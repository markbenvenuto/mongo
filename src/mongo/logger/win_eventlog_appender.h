/*    Copyright 2014 10gen Inc.
*
*    Licensed under the Apache License, Version 2.0 (the "License");
*    you may not use this file except in compliance with the License.
*    You may obtain a copy of the License at
*
*    http://www.apache.org/licenses/LICENSE-2.0
*
*    Unless required by applicable law or agreed to in writing, software
*    distributed under the License is distributed on an "AS IS" BASIS,
*    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*    See the License for the specific language governing permissions and
*    limitations under the License.
*/

#pragma once

#ifdef _WIN32

#include <boost/scoped_ptr.hpp>
#include <sstream>

#include "mongo/base/disallow_copying.h"
#include "mongo/base/status.h"
#include "mongo/logger/appender.h"
#include "mongo/logger/encoder.h"

namespace mongo {
    namespace logger {
#define EVENT_PROVIDER_NAME "MongoDB"
        /**
        * Appender for writing to the console (stdout).
        */
        template <typename Event>
        class WinEventLogAppender : public Appender < Event > {
            MONGO_DISALLOW_COPYING(WinEventLogAppender);

        public:
            typedef Encoder<Event> EventEncoder;


            explicit WinEventLogAppender(EventEncoder* encoder) : _encoder(encoder) {
                m_handle = RegisterEventSourceA(NULL, EVENT_PROVIDER_NAME);
            }

            ~WinEventLogAppender() {
                ::DeregisterEventSource(m_handle);
            }

            virtual Status append(const Event& event) {
                std::ostringstream os;
                _encoder->encode(event, os);
                if (!os)
                    return Status(ErrorCodes::LogWriteFailed, "Error writing log message to event log.");

                if (m_handle)
                {
                    WORD wType = getEventPriority(event.getSeverity());

                    LPCSTR lpszStrings[2] = { NULL, NULL };
                    lpszStrings[0] = EVENT_PROVIDER_NAME;
                    string stupid = os.str();
                    lpszStrings[1] = stupid.c_str();

                    ReportEventA(m_handle,  // Event log handle
                        wType,                 // Event type
                        0,                     // Event category
                        0,                     // Event identifier
                        NULL,                  // No security identifier
                        2,                     // Size of lpszStrings array
                        0,                     // No binary data
                        lpszStrings,           // Array of strings
                        NULL                   // No binary data
                        );
                }

                return Status::OK();
            }

            int getEventPriority(LogSeverity severity) {
                if (severity <= LogSeverity::Debug(1))
                    return EVENTLOG_INFORMATION_TYPE;
                if (severity == LogSeverity::Warning())
                    return EVENTLOG_WARNING_TYPE;
                if (severity == LogSeverity::Error())
                    return EVENTLOG_ERROR_TYPE;
                if (severity >= LogSeverity::Severe())
                    return EVENTLOG_ERROR_TYPE;
                // Info() and Log().
                return EVENTLOG_INFORMATION_TYPE;
            }
        private:
            HANDLE m_handle;
            boost::scoped_ptr<EventEncoder> _encoder;
        };
    }

#if 0
    void WriteEventLogEntry(std::string pszMessage, WORD wType)
    {
        HANDLE hEventSource = NULL;
        LPCSTR lpszStrings[2] = { NULL, NULL };

        hEventSource = RegisterEventSourceA(NULL, EVENT_PROVIDER_NAME);
        if (hEventSource)
        {
            lpszStrings[0] = EVENT_PROVIDER_NAME;
            lpszStrings[1] = pszMessage.c_str();

            ReportEventA(hEventSource,  // Event log handle 
                wType,                 // Event type 
                0,                     // Event category 
                0,                     // Event identifier 
                NULL,                  // No security identifier 
                2,                     // Size of lpszStrings array 
                0,                     // No binary data 
                lpszStrings,           // Array of strings 
                NULL                   // No binary data 
                );

            DeregisterEventSource(hEventSource);
        }
    }
#endif
}
#endif
