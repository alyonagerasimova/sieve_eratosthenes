/*--------------------------------------------------------------------------*/
/*  Copyright 2020-2021 Sergei Vostokin                                     */
/*                                                                          */
/*  Licensed under the Apache License, Version 2.0 (the "License");         */
/*  you may not use this file except in compliance with the License.        */
/*  You may obtain a copy of the License at                                 */
/*                                                                          */
/*  http://www.apache.org/licenses/LICENSE-2.0                              */
/*                                                                          */
/*  Unless required by applicable law or agreed to in writing, software     */
/*  distributed under the License is distributed on an "AS IS" BASIS,       */
/*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.*/
/*  See the License for the specific language governing permissions and     */
/*  limitations under the License.                                          */
/*--------------------------------------------------------------------------*/
#pragma once

#include <vector>
#include <list>
#include <queue>
#include <cstdlib>
#include <cassert>

#if   defined(TEMPLET_CPP_SYNC)
#include <mutex>
#elif defined(TEMPLET_OMP_SYNC)
#include <omp.h>
#endif

namespace templet{
	
	class message;
	class actor;
	class engine;
	class task{};

	class base_engine;
	class base_task;

#if   defined(TEMPLET_CPP_SYNC)
	class mutex_mock : public std::mutex {};
#elif defined(TEMPLET_OMP_SYNC)
	class mutex_mock {
	public:
		mutex_mock() { omp_init_lock(&a_lock); }
		~mutex_mock() { omp_destroy_lock(&a_lock); }
		inline bool try_lock() { return omp_test_lock(&a_lock)!=0; }
		inline void lock() { omp_set_lock(&a_lock); }
		inline void unlock() { omp_unset_lock(&a_lock); }
	private:
		omp_lock_t a_lock;
	};
#else
	class mutex_mock {
	public:
		inline bool try_lock() { return true; }
		inline void lock(){}
		inline void unlock(){}
	};
#endif

	typedef void(*message_adaptor)(actor*, message*);
	typedef void(*task_adaptor)(actor*, task*);

	class message {
		friend class engine;
		friend class actor;
	public:
		message(actor*a=0, message_adaptor ma=0) :_from_cli_to_srv(true),
			_cli_actor(a),  _cli_adaptor(ma),
			_srv_actor(0),  _srv_adaptor(0),
			_active(false), _actor(a) {}
		void send();
		void bind(templet::actor*a) { assert(_actor == 0); _actor = a; }
		void bind(templet::actor*a, message_adaptor ma) {
			_from_cli_to_srv = true; _srv_actor = a; _srv_adaptor = ma;
		}
	private:
		bool _active;
		templet::actor* _actor;
		message_adaptor _adaptor;

		bool _from_cli_to_srv;
		templet::actor* _cli_actor;
		templet::actor* _srv_actor;
		message_adaptor _cli_adaptor;
		message_adaptor _srv_adaptor;
	};

	class actor {
		friend class engine;
	public:
		actor(bool start_it=false) :_suspended(false), _start_it(start_it), _engine(0) {}
	protected:
		void engine(templet::engine&e);
		void engine(templet::engine*e);
		virtual void start() {};
	public:
		bool access(message&m) const { return  m._actor == this && !m._active; }
		bool access(message*m) const { return m && m->_actor == this && !m->_active; }
		void stop();
		void suspend() { assert(!_suspended); _suspended = true; }
		void resume();
	private:
		bool _suspended;
		bool _start_it;
		std::queue<message*> _in_queue;
		std::queue<message*> _out_queue;
		templet::engine*     _engine;
	};
	
	class engine {
		friend class message;
		friend class actor;
	public:
		engine() : _stopped(false), _started(false) {}
		~engine(){}

		void start(){
			if (!_started) {
				_mes_lock.lock();
		
				for (std::list<actor*>::iterator it = _start_list.begin(); it != _start_list.end(); it++)
					(*it)->start();
		
				while (!_mes_queue.empty()) {
					message*m = _mes_queue.front(); _mes_queue.pop();
					recv(m);
					if (_stopped) { _mes_lock.unlock(); break; }
				}

				_started = true;
				_mes_lock.unlock();
			}
		}

		bool stopped() { return _stopped; }

	private:
		mutex_mock _mes_lock;
		mutex_mock _act_lock;
		bool _stopped;
		bool _started;
		std::list<actor*>    _start_list;
		std::queue<message*> _mes_queue;
		std::queue<actor*>   _act_queue;

	private:
		inline static void send(message*m) {
			assert(!m->_active);
			
			actor* sender_actor = m->_actor;

			m->_active = true;
			m->_actor = (m->_from_cli_to_srv ? m->_srv_actor : m->_cli_actor);
			m->_adaptor = (m->_from_cli_to_srv ? m->_srv_adaptor : m->_cli_adaptor);

			if (sender_actor->_suspended) 
				sender_actor->_out_queue.push(m);
			else {
				engine* eng = sender_actor->_engine;
				eng->_mes_queue.push(m);
			}
		}

		inline static void recv(message*m) {
			actor* receiver_actor = m->_actor;

			if (receiver_actor->_suspended)
				receiver_actor->_in_queue.push(m);
			else {
				m->_active = false;
				m->_from_cli_to_srv = !m->_from_cli_to_srv;
				(*m->_adaptor)(receiver_actor,m);
			}
		}

#ifndef SIMPLE_RESUME
        inline static void resume(actor*a) {
			engine* eng = a->_engine;

			while(!eng->_act_lock.try_lock());
			if (eng->_stopped) { eng->_act_lock.unlock(); return; }

			eng->_act_queue.push(a);

		try_get_the_control:
			if (eng->_mes_lock.try_lock()) {

				while (!eng->_act_queue.empty()) {
					actor* a = eng->_act_queue.front(); eng->_act_queue.pop();

					while (!a->_out_queue.empty()) {
						message*m = a->_out_queue.front(); a->_out_queue.pop();
						eng->_mes_queue.push(m);
					}

					while (!a->_in_queue.empty()) {
						message*m = a->_in_queue.front(); a->_in_queue.pop();
						eng->_mes_queue.push(m);
					}

					a->_suspended = false;
				}

				eng->_act_lock.unlock();

				while (!eng->_mes_queue.empty()) {
					message*m = eng->_mes_queue.front(); eng->_mes_queue.pop();
					recv(m);
					if (eng->_stopped) { eng->_mes_lock.unlock(); return; }
				}

				eng->_mes_lock.unlock();
			}
			else {
				eng->_act_lock.unlock();
				return;
			}

			while(!eng->_act_lock.try_lock());

			if (eng->_act_queue.empty()) {
				eng->_act_lock.unlock();
				return;
			}
			else
				goto try_get_the_control;
		}
#else // SIMPLE RESUME
		inline static void resume(actor*a) {
			engine* eng = a->_engine;

			eng->_mes_lock.lock();
			if (eng->_stopped) { eng->_mes_lock.unlock(); return; }
	
			while (!a->_out_queue.empty()) {
				message*m = a->_out_queue.front(); a->_out_queue.pop();
				eng->_mes_queue.push(m);
			}

			while (!a->_in_queue.empty()) {
				message*m = a->_in_queue.front(); a->_in_queue.pop();
				eng->_mes_queue.push(m);
			}

			a->_suspended = false;

			while (!eng->_mes_queue.empty()) {
				message*m = eng->_mes_queue.front(); eng->_mes_queue.pop();
				recv(m);
				if (eng->_stopped) { eng->_mes_lock.unlock(); return; }
			}

			eng->_mes_lock.unlock();
		}
#endif
	};

	void message::send() {	engine::send(this); }
	void actor::engine(templet::engine&e) { _engine = &e; if(this->_start_it)_engine->_start_list.push_back(this); }
	void actor::engine(templet::engine*e) { _engine = e; if(this->_start_it)_engine->_start_list.push_back(this);	}
	void actor::stop() { _engine->_stopped = true;	}
	void actor::resume() { engine::resume(this); }

	class base_task: task {
		friend class base_engine;
	public:
		base_task(actor*a,task_adaptor ta) :_actor(a), _tsk_adaptor(ta), _engine(0), _idle(true){}
		void engine(base_engine&e) { assert(_engine==0); _engine = &e; }
		void submit();
	protected:
		virtual void run(){}
	private:
		bool         _idle;
		actor*       _actor;
		base_engine* _engine;
		task_adaptor _tsk_adaptor;
	};

	class base_engine {
		friend class base_task;
	public:
		bool running() {
			size_t rsize = _task_queue.size();	
			if (rsize) {
				int n = rand() % rsize;
				std::vector<base_task*>::iterator it = _task_queue.begin() + n;
				base_task* tsk = *it; _task_queue.erase(it);

				tsk->run();
				(*tsk->_tsk_adaptor)(tsk->_actor, tsk);
				tsk->_idle = true;

				tsk->_actor->resume();
				return true;
			}
			return false;
		}
		void run() { while (running());	}
	private:
		std::vector<base_task*> _task_queue;
	};

	void base_task::submit() { 
		assert(_idle); 
		_actor->suspend(); 
		_engine->_task_queue.push_back(this); 
		_idle = false; 
	}
}