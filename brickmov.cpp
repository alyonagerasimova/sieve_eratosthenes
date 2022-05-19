/*$TET$$header*/
/*--------------------------------------------------------------------------*/
/*  Copyright 2021 Sergei Vostokin                                          */
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

const int NUMBER_OF_MEDIATORS = 50;
const int NUMBER_OF_BRICKS = 2;
const int DELAY = 1;

#include <templet.hpp>
#include <basesim.hpp>
#include <cmath>
#include <iostream>
#include <ctime>

class brick : public templet::message {
public:
    brick(templet::actor *a = 0, templet::message_adaptor ma = 0) : templet::message(a, ma) {}

    int brick_ID;
};
/*$TET$*/

#pragma templet !source(out!brick)

struct source : public templet::actor {
    static void on_out_adapter(templet::actor *a, templet::message *m) {
        ((source *) a)->on_out(*(brick *) m);
    }

    source(templet::engine &e) : source() {
        source::engines(e);
    }

    source() : templet::actor(true),
               out(this, &on_out_adapter) {
/*$TET$source$source*/
/*$TET$*/
    }

    void engines(templet::engine &e) {
        templet::actor::engine(e);
/*$TET$source$engines*/
/*$TET$*/
    }

    void start() {
/*$TET$source$start*/
        on_out(out);
/*$TET$*/
    }

    inline void on_out(brick &m) {
/*$TET$source$out*/
        if (number_of_bricks > 0) {
            out.brick_ID = number_of_bricks++;
            out.send();

            //std::cout << "the source worker passes a brick #" << m.brick_ID << std::endl;
        }
/*$TET$*/
    }

    brick out;

/*$TET$source$$footer*/
    int number_of_bricks;
/*$TET$*/
};

#pragma templet mediator(in?brick, out!brick, t:basesim)

struct mediator : public templet::actor {
    static void on_in_adapter(templet::actor *a, templet::message *m) {
        ((mediator *) a)->on_in(*(brick *) m);
    }

    static void on_out_adapter(templet::actor *a, templet::message *m) {
        ((mediator *) a)->on_out(*(brick *) m);
    }

    static void on_t_adapter(templet::actor *a, templet::task *t) {
        ((mediator *) a)->on_t(*(templet::basesim_task *) t);
    }

    mediator(templet::engine &e, templet::basesim_engine &te_basesim) : mediator() {
        mediator::engines(e, te_basesim);
    }

    mediator() : templet::actor(false),
                 out(this, &on_out_adapter),
                 t(this, &on_t_adapter) {
/*$TET$mediator$mediator*/
        _in = 0;
/*$TET$*/
    }

    void engines(templet::engine &e, templet::basesim_engine &te_basesim) {
        templet::actor::engine(e);
        t.engine(te_basesim);
/*$TET$mediator$engines*/
/*$TET$*/
    }

    inline void on_in(brick &m) {
/*$TET$mediator$in*/
        _in = &m;
        take_a_brick();
/*$TET$*/
    }

    inline void on_out(brick &m) {
/*$TET$mediator$out*/
        take_a_brick();
/*$TET$*/
    }

    inline void on_t(templet::basesim_task &t) {
/*$TET$mediator$t*/
        t.delay(DELAY);
        pass_a_brick();
/*$TET$*/
    }

    void in(brick &m) { m.bind(this, &on_in_adapter); }

    brick out;
    templet::basesim_task t;

/*$TET$mediator$$footer*/
    void take_a_brick() {
        if (access(_in) && access(out)) {

            brick_ID = _in->brick_ID;
            _in->send();

            t.submit();
        }
    }

    void pass_a_brick() {
        if (prime) {
            if (brick_ID % prime == 0) return;
            out.brick_ID = brick_ID;
            out.send();
        } else {
            prime = brick_ID;
            std::cout << "the worker #"
                      << mediator_ID << " wrote a prime number : " << prime << std::endl;
        }
    }

    brick *_in;
    int brick_ID;
    int mediator_ID;
    int prime;
/*$TET$*/
};

#pragma templet destination(in?brick)

struct destination : public templet::actor {
    static void on_in_adapter(templet::actor *a, templet::message *m) {
        ((destination *) a)->on_in(*(brick *) m);
    }

    destination(templet::engine &e) : destination() {
        destination::engines(e);
    }

    destination() : templet::actor(false) {
/*$TET$destination$destination*/
        number_of_bricks = 0;
/*$TET$*/
    }

    void engines(templet::engine &e) {
        templet::actor::engine(e);
/*$TET$destination$engines*/
/*$TET$*/
    }

    inline void on_in(brick &m) {
/*$TET$destination$in*/
        std::cout << "the destination worker found a prime #" << m.brick_ID << std::endl;
        if (number_of_bricks == 0) stop();
        else m.send();
        std::cout << "the destination worker takes a prime #" << m.brick_ID << std::endl;
        /*$TET$*/
    }

    void in(brick &m) { m.bind(this, &on_in_adapter); }

/*$TET$destination$$footer*/
    int number_of_bricks;
/*$TET$*/
};

/*$TET$$footer*/

int main() {
    templet::engine eng;
    templet::basesim_engine teng;

    source source_worker(eng);
    mediator mediator_worker[NUMBER_OF_MEDIATORS];
    destination destination_worker(eng);

    mediator_worker[0].in(source_worker.out);

    for (int i = 1; i < NUMBER_OF_MEDIATORS; i++)
        mediator_worker[i].in(mediator_worker[i - 1].out);

    destination_worker.in(mediator_worker[NUMBER_OF_MEDIATORS - 1].out);

    source_worker.number_of_bricks = NUMBER_OF_BRICKS;

    for (int i = 0; i < NUMBER_OF_MEDIATORS; i++) {
        mediator_worker[i].mediator_ID = i + 1;
        mediator_worker[i].engines(eng, teng);
    }

    eng.start();
    teng.run();

    if (eng.stopped()) {
        std::cout << "done !!!" << std::endl;

        std::cout << "Maximum number of tasks executed in parallel : " << teng.Pmax() << std::endl;
        std::cout << "Time of sequential execution of all tasks    : " << teng.T1() << std::endl;
        std::cout << "Time of parallel   execution of all tasks    : " << teng.Tp() << std::endl;
        std::cout << "Speedup of parallel execution    : " << teng.T1() / teng.Tp() << std::endl;
        std::cin.get();
        return EXIT_SUCCESS;
    }

    std::cout << "something broke (((" << std::endl;
    return EXIT_FAILURE;
}
/*$TET$*/