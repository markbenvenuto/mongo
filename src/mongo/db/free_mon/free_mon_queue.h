/**
 * Copyright (C) 2018 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#pragma once

#include <boost/optional.hpp>
#include <thread>
#include <queue>
#include <vector>

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/free_mon/free_mon_network.h"
#include "mongo/db/free_mon/free_mon_message.h"

#include "mongo/util/clock_source.h"
#include "mongo/util/duration.h"
#include "mongo/util/future.h"

namespace mongo {


struct FreeMonMessageGreater {
    bool operator()(const FreeMonMessage& left, const FreeMonMessage& right) const {
        return (left.getDeadline() > right.getDeadline());
    }
    bool operator()(const std::shared_ptr<FreeMonMessage>& left,
                    const std::shared_ptr<FreeMonMessage>& right) const {
        return (left->getDeadline() > right->getDeadline());
    }
};

// Multi-Producer
// Single Consumer
class FreeMonMessageQueue {
public:
    FreeMonMessageQueue();
    void enqueue(std::shared_ptr<FreeMonMessage> msg);
    boost::optional<std::shared_ptr<FreeMonMessage>> dequeue(ClockSource* clockSource);

    void stop();

private:
    bool _stop;
    std::condition_variable _condvar;
    std::mutex _mutex;

    std::priority_queue<std::shared_ptr<FreeMonMessage>,
                        std::vector<std::shared_ptr<FreeMonMessage>>,
                        FreeMonMessageGreater>
        _queue;
};


} // namespace mongo
