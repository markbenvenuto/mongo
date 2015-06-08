/**
 *    Copyright (C) 2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

 /**
  * Initialization code for the ICU (International Components for Unicode) library.
  * Particularly, loading the data from an embedded resource in Windows.
  */

#include <unicode/udata.h>

#include "mongo/base/init.h"

#ifdef _WIN32

#include <windows.h>

#define ICU_DATA_RES 300

#endif

namespace mongo {

    MONGO_INITIALIZER(LoadICUData)(InitializerContext* context) {

    #ifdef _WIN32

        // Load the resource from the executable
        HGLOBAL res_handle = NULL;
        HRSRC res;
        void *res_data;

        res = FindResource(NULL, MAKEINTRESOURCE(ICU_DATA_RES), RT_RCDATA);
        if (!res) {
            printf("Failed to load resource (1).\n")
            return Status::OK();
        }
        res_handle = LoadResource(NULL, res);
        if (!res_handle) {
            printf("Failed to load resource (2).\n")
            return Status::OK();
        }
        res_data = LockResource(res_handle);

        UErrorCode icu_err = U_ZERO_ERROR;

        // Set the ICU data 
        udata_setCommonData(res_data, &icu_err);

        if(err != U_ZERO_ERROR) {
            printf("Failed to load resource (3).\n");
            return Status::OK();
        }

    #else 
        // Data library is statically linked by default on non-Windows platforms
        // TODO: There might be slightly different behavior when using system ICU

    #endif

        return Status::OK();

    }

} // namespace mongo
