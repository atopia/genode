/*
 * \brief  AIO implementation
 * \author Benjamin Lamowski
 * \date   2024-06-18
 */

/*
 * Copyright (C) 2024 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

/* Libc includes */
#include <errno.h>
#include <sys/types.h>
#include <aio.h>
#include <sys/event.h>

/* internal includes */
#include <internal/file.h>
#include <internal/kernel.h>
#include <internal/kqueue.h>
#include <internal/monitor.h>

#include <base/semaphore.h>
#include <base/thread.h>
#include <base/mutex.h>
#include <util/avl_tree.h>
#include <util/reconstructible.h>

#include <vfs/file_io_service.h>

using namespace Libc;


namespace Libc {
	class Aio;
}

namespace { using Fn = Libc::Monitor::Function_result; }

static Monitor      *_monitor_ptr;
static Libc::Signal *_signal_ptr;

static Libc::Monitor & monitor()
{
	struct Missing_call_of_init_aio_support : Genode::Exception { };
	if (!_monitor_ptr)
		throw Missing_call_of_init_aio_support();
	return *_monitor_ptr;
}

static int check_aiocb(struct aiocb *iocb)
{
	if (!file_descriptor_allocator()->find_by_libc_fd(iocb->aio_fildes))
		return Errno(EOPNOTSUPP);

        /* For now we don't support SIGEV_SIGNO and SIGEV_THREAD */
	if (iocb->aio_sigevent.sigev_notify != SIGEV_KEVENT &&
	    iocb->aio_sigevent.sigev_notify != SIGEV_NONE) {
		warning("AIO: Unsupported notification type");
		return Errno(EINVAL);
	}

	return 0;
}

/*
 * Manage AIO jobs and results
 */
class Libc::Aio
{
	private:

		struct Request
		{
			enum class Type {
				Read,
				Write,
				Fsync,
			};

			aiocb * aiocb_ptr;
			int error { EINPROGRESS };
			ssize_t retval { 0 };
			Type type;
			union {
				Plugin::Async_read_state  read_state;
				Plugin::Async_write_state write_state;
			};
			/* Sync needs to be constructed outside the monitor context */
			Constructible<Libc::Vfs_plugin::Sync> sync;

			struct Async_blockade : public Blockade
			{
				void block() override
				{
					Genode::error("Trying to block async job.");
				}

				void wakeup() override
				{
					_woken_up = true;
				}
			} _blockade;

			struct _Function : Libc::Monitor::Function
			{
				Request & request;
				Libc::Monitor::Function_result execute() override
				{
					void * buf = (void *) request.aiocb_ptr->aio_buf;
					size_t const nbytes = request.aiocb_ptr->aio_nbytes;
					off_t offset = request.aiocb_ptr->aio_offset;
					File_descriptor *fd = libc_fd_to_fd(request.aiocb_ptr->aio_fildes, "aio");

					if (!fd || !fd->plugin) {
						request.error = EBADF;
						request.retval = -1;
						return Fn::COMPLETE;
					}

					bool complete = true;
					switch(request.type) {
					case Type::Read:
						complete = fd->plugin->async_read(fd, buf, nbytes, offset ,request.retval, request.error, request.read_state);
						break;
					case Type::Write:
						complete = fd->plugin->async_write(fd, buf, nbytes, offset ,request.retval, request.error, request.write_state);
						break;
					case Type::Fsync:
						if (!request.sync.constructed())
							return Fn::COMPLETE;
						complete = request.sync->complete();
					}

					if (complete) {
						if (request.aiocb_ptr->aio_sigevent.sigev_notify == SIGEV_KEVENT) {
							int kqueue_fd = request.aiocb_ptr->aio_sigevent.sigev_notify_kqueue;
							int flags = request.aiocb_ptr->aio_sigevent.sigev_notify_kevent_flags;
							struct kevent event;
							EV_SET(&event, request.aiocb_ptr->aio_fildes, EVFILT_AIO, EV_ADD | EV_CLEAR | flags, 0, 0,
							request.aiocb_ptr->aio_sigevent.sigev_value.sigval_ptr);

							if (-1 == Kqueue_plugin::process_single_event(kqueue_fd, &event))
								warning("AIO: failed to add kevent notification");
						}
					}

					return complete ? Fn::COMPLETE : Fn::INCOMPLETE;
				}
				_Function(Request & request) : request(request)
				{};
			} _function;

			Constructible<Libc::Monitor::Job> job;

			Request(aiocb *ptr, Type type) : aiocb_ptr(ptr), type(type), _function(*this)
			{
				job.construct(_function, _blockade);
				monitor().monitor_async(*job);
				if (type == Type::Fsync) {
					File_descriptor *fd = libc_fd_to_fd(aiocb_ptr->aio_fildes, "aio");
					if (!fd || !fd->plugin) {
						error = EBADF;
						retval = -1;
						return;
					}
					sync.construct(*reinterpret_cast<Vfs::Vfs_handle *>(fd->context), *(fd->plugin));
				}
			}

			Request(Request const &) = delete;
			Request &operator = (Request const &) = delete;
		};

		struct Aio_requests;

		struct Aio_element : Request, public Avl_node<Aio_element>
		{
			private:

				Aio_element *_find_by_ptr(aiocb const * ptr)
				{
					if (ptr == aiocb_ptr) return this;
					Aio_element *ele = this->child(ptr > aiocb_ptr);
					return ele ? ele->_find_by_ptr(ptr) : nullptr;
				}

			public:

				Aio_element(aiocb * const aiocb, Type type)
				:	Request(aiocb, type)
				{ }

				bool higher(Aio_element *e)
				{
					return e->aiocb_ptr > aiocb_ptr;
				}

				static Aio_element * find_by(Aio_requests &map, aiocb const * ptr)
				{
					Aio_element * head = map.first();
					if (!head)
						return head;

					return head->_find_by_ptr(ptr);
				}
		};

		struct Aio_requests : Avl_tree<Aio_element>
		{
			bool with_any_element(auto const &fn)
			{
				Aio_element *curr_ptr = first();
				if (!curr_ptr)
					return false;

				fn(*curr_ptr);
				return true;
			}

			template <typename FN>
			auto with_element(aiocb const * ptr, FN const &match_fn, auto const &no_match_fn)
			-> typename Trait::Functor<decltype(&FN::operator())>::Return_type
			{
				Aio_element *ele = Aio_element::find_by(*this, ptr);
				if (ele)
					return match_fn(*ele);
				else
					return no_match_fn();
			}
		};


		Genode::Allocator & _alloc;

		Mutex _requests_mutex;
		Aio_requests _requests;

		void _insert(aiocb *ptr, Request::Type type)
		{
			Aio_element *ele = new (_alloc) Aio_element(ptr, type);
			{
				Mutex::Guard guard(_requests_mutex);
				_requests.insert(ele);
			}
		}

		int _cancel(Aio_element & ele)
		{
			ele.job.destruct();

			if (ele.error == EINPROGRESS) {
				ele.error = ECANCELED;
				return AIO_CANCELED;
			} else {
				return AIO_ALLDONE;
			}
		}

	public:

		Aio(Genode::Allocator & alloc) : _alloc(alloc)
		{ }

		~Aio()
		{
			auto destroy_fn = [&] (Aio_element &e) {
				_requests.remove(&e);
				destroy(_alloc, &e); };
			while (_requests.with_any_element(destroy_fn));
		}


		void insert_read(aiocb *ptr)
		{
			_insert(ptr, Request::Type::Read);
		}

		void insert_write(aiocb *ptr)
		{
			_insert(ptr, Request::Type::Write);
		}

		void insert_fsync(aiocb *ptr)
		{
			_insert(ptr, Request::Type::Fsync);
		}

		void insert_fdatasync(aiocb *ptr)
		{
			/* Our libc aliases fdatasync to fsync */
			_insert(ptr, Request::Type::Fsync);
		}


		int get_error(aiocb const * ptr)
		{
			auto match_fn = [](Aio_element & ele) {
				return ele.error;
			};

			auto no_match_fn = []() {
				return Errno(EINVAL);
			};

			return _requests.with_element(ptr, match_fn, no_match_fn);
		}

		ssize_t get_return_and_remove(aiocb *ptr)
		{
			auto match_fn = [&](Aio_element & ele) {
				if (ele.error == EINPROGRESS)
					return (ssize_t) Errno(EINVAL);

				ssize_t retval = ele.retval;
				_requests.remove(&ele);
				destroy(_alloc, &ele);

				return retval;
			};

			auto no_match_fn = []() {
				return Errno(EINVAL);
			};

			return _requests.with_element(ptr, match_fn, no_match_fn);
		}

		int cancel(aiocb *ptr)
		{
			auto match_fn = [&](Aio_element & ele) {
				return _cancel(ele);
			};

			auto no_match_fn = []() {
				return Errno(EINVAL);
			};

			return _requests.with_element(ptr, match_fn, no_match_fn);
		}

		int cancel(int fd);

		/*
		 * Returns 0 if the AIO request is completed,
		 * 1 if it is still in progress and
		 * -1 if an error occured.
		 */
		int check_completed(aiocb const *ptr);
};



int Aio::cancel(int fd)
{
	int retval_all = Errno(EBADF);

	struct Aio_element_container : Fifo<Aio_element_container>::Element
	{
		Aio_element const & ele;
		Aio_element_container(Aio_element const & e) : ele(e)
		{ }
	};

	Fifo<Aio_element_container> _delete_queue;

	/*
	 * We need to first collect the Aio_element into a queue because
	 * deleting them will reshuffle the Aio tree
	 */
	auto cancel_collect_fn = [&](Aio_element const & ele) {
		if (ele.aiocb_ptr->aio_fildes == fd) {
			Aio_element_container &c =
				*new (_alloc) Aio_element_container(ele);
			_delete_queue.enqueue(c);
		}
	};

	_requests.for_each(cancel_collect_fn);

	auto cancel_fn = [&](Aio_element_container & c) {
		/*
		 * At his point for_each() has run and it is safe to const-cast
		 * the element in order to delete it.
		 */
		int retval = _cancel(const_cast<Aio_element &>(c.ele));
		destroy(_alloc, &c);
		switch (retval_all) {
		case -1:
			retval_all = retval;
			break;
		/* another FD has already been cancelled */
		case AIO_CANCELED:
			if (retval != AIO_CANCELED)
				retval_all = AIO_NOTCANCELED;
			break;
		case AIO_ALLDONE:
			if (retval != AIO_ALLDONE)
				retval_all = AIO_NOTCANCELED;
			break;
		default:
			break;
		}
	};

	_delete_queue.dequeue_all(cancel_fn);

	if (retval_all != -1)
		errno = 0;
	return retval_all;
}

int Aio::check_completed(aiocb const *ptr)
{
	auto match_fn = [&](Aio_element & ele) {
		if (ele.error != EINPROGRESS)
			return 0;
		else
			return 1;
	};

	auto no_match_fn = []() {
		return Errno(EINVAL);
	};

	return _requests.with_element(ptr, match_fn, no_match_fn);
}


/*
 * static AIO backend
 */
static Aio * _aio_backend_ptr;

static Aio * aio_backend()
{
	if (!_aio_backend_ptr) {
		error("libc AIO not initialized - aborting");
		exit(1);
	}

	return _aio_backend_ptr;
}

void Libc::init_aio(Genode::Allocator & alloc, Monitor &monitor, Signal &signal)
{
	_aio_backend_ptr = new (alloc) Aio(alloc);
	_monitor_ptr     = &monitor;
	_signal_ptr      = &signal;
}

extern "C" int
aio_read(struct aiocb *iocb)
{
	if (check_aiocb(iocb))
		return -1;

	aio_backend()->insert_read(iocb);

	return 0;
}


extern "C" int
aio_write(struct aiocb *iocb)
{
	if (check_aiocb(iocb))
		return -1;

	aio_backend()->insert_write(iocb);
	return 0;
}

extern "C" int
aio_fsync(int op, struct aiocb *iocb)
{
	/*
	 * O_DSYNC is not defined in our libc but may be used by callers of the
	 * function
	 */
	enum { O_DSYNC = 0x01000000 };

	if (check_aiocb(iocb))
		return -1;

	switch (op) {
	case O_SYNC:
		aio_backend()->insert_fsync(iocb);
		break;
	case O_DSYNC:
		aio_backend()->insert_fdatasync(iocb);
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	return 0;
}

extern "C" int
aio_error( const struct aiocb *iocb)
{
	int retval = aio_backend()->get_error(iocb);

	if (retval == -1)
		errno = EINVAL;

	return retval;
}

extern "C" ssize_t
aio_return(struct aiocb *iocb)
{
	if (aio_error(iocb) == EINPROGRESS)
		return Errno(EINVAL);
	 else
		return aio_backend()->get_return_and_remove(iocb);
}

extern "C" int
aio_cancel(int fildes, struct aiocb *iocb)
{
	if (iocb) {
		if (fildes != iocb->aio_fildes)
			return Errno(EBADF);

		return aio_backend()->cancel(iocb);
	} else
		return aio_backend()->cancel(fildes);
}

extern "C" int
aio_suspend(const struct aiocb *const iocbs[],int niocb,
            const struct timespec *timeout)
{
	/*
	 * XXX The handling of timeouts and signals is a candidate for merging
	 * with poll() (and maybe others).
	 */
	int timeout_ms = timeout ?
	                 (timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000) :
	                 -1;

	if (timeout_ms == 0)
		return Errno(EAGAIN);

	/* convert infinite timeout to monitor interface */
	if (timeout_ms < 0)
		timeout_ms = 0;

	/*
	 * Use 1 to signal no error but no completion either.
	 * An error will set errno and set retval to -1.
	 * If a completed element is found, retval is set
	 * to 0.
	 * Either result will break the loop and return.
	 * This means that a completed AIO request can mask
	 * invalid aiocb pointers at a later position in the array.
	 */
	int retval { 1 };

	unsigned const orig_signal_count = _signal_ptr->count();

	auto signal_occurred_during_poll = [&] ()
	{
		return (_signal_ptr->count() != orig_signal_count);
	};

	auto monitor_fn = [&] ()
	{

		for (int i = 0; i < niocb; i++) {
			retval = aio_backend()->check_completed(iocbs[i]);

			if (retval != 1)
				return Monitor::Function_result::COMPLETE;
		}

		if (signal_occurred_during_poll())
			return Monitor::Function_result::COMPLETE;

		return Monitor::Function_result::INCOMPLETE;
	};

	Monitor::Result const monitor_result =
		monitor().monitor(monitor_fn, timeout_ms);

	if (monitor_result == Monitor::Result::TIMEOUT)
		return Errno(EAGAIN);

	if (signal_occurred_during_poll())
		return Errno(EINTR);

	return retval;
}
