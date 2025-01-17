/* Copyright 2021 Jaakko Keränen <jaakko.keranen@iki.fi>

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "sitespec.h"

#include <the_Foundation/file.h>
#include <the_Foundation/path.h>
#include <the_Foundation/stringhash.h>

iDeclareClass(SiteParams)
iDeclareObjectConstruction(SiteParams)
    
struct Impl_SiteParams {
    iObject  object;
    uint16_t titanPort;
    /* TODO: theme seed, style settings */
};

void init_SiteParams(iSiteParams *d) {
    d->titanPort = 0; /* undefined */
}

void deinit_SiteParams(iSiteParams *d) {
    iUnused(d);
}

iDefineClass(SiteParams)
iDefineObjectConstruction(SiteParams)
    
/*----------------------------------------------------------------------------------------------*/
    
struct Impl_SiteSpec {
    iString     savePath;
    iStringHash sites;
};

static iSiteSpec siteSpec_;

static void load_SiteSpec_(iSiteSpec *d) {
    iFile *f = iClob(new_File(&d->savePath));
    if (open_File(f, readOnly_FileMode | text_FileMode)) {
        iString *src = collect_String(readString_File(f));
        iRangecc split = iNullRange;
        iString *key = collectNew_String();
        iSiteParams *params = NULL;
        while (nextSplit_Rangecc(range_String(src), "\n", &split)) {
            iRangecc line = split;
            trim_Rangecc(&line);
            if (isEmpty_Range(&line)) {
                continue;
            }
            if (startsWith_Rangecc(line, "# ")) {
                if (params && !isEmpty_String(key)) {
                    insert_StringHash(&d->sites, key, params);
                    iReleasePtr(&params);
                }
                line.start += 2;
                setRange_String(key, line);
                params = new_SiteParams();
                continue;
            }
            if (startsWith_Rangecc(line, "titanPort: ")) {
                line.start += 11;
                if (params) {
                    params->titanPort = atoi(cstr_Rangecc(line));
                }
                continue;                
            }
        }
        if (params && !isEmpty_String(key)) {
            insert_StringHash(&d->sites, key, params);
            iReleasePtr(&params);
        }
    }
}

static void save_SiteSpec_(iSiteSpec *d) {
    iFile *f = new_File(&d->savePath);
    if (open_File(f, writeOnly_FileMode | text_FileMode)) {
        iString *buf = new_String();
        iConstForEach(StringHash, i, &d->sites) {
            const iBlock *     key    = &i.value->keyBlock;
            const iSiteParams *params = i.value->object;
            format_String(buf, "# %s\n", cstr_Block(key));
            appendFormat_String(buf, "titanPort: %u\n", params->titanPort);
            write_File(f, utf8_String(buf));
        }
        delete_String(buf);
    }
    iRelease(f);
}

void init_SiteSpec(const char *saveDir) {
    iSiteSpec *d = &siteSpec_;
    initCStr_String(&d->savePath, concatPath_CStr(saveDir, "sitespec.txt"));
    init_StringHash(&d->sites);
    load_SiteSpec_(d);
}

void deinit_SiteSpec(void) {
    iSiteSpec *d = &siteSpec_;
    deinit_StringHash(&d->sites);
    deinit_String(&d->savePath);
}

void setValue_SiteSpec(const iString *site, enum iSiteSpecKey key, int value) {
    iSiteSpec *d = &siteSpec_;
    const iString *hashKey = collect_String(lower_String(site));
    iSiteParams *params = value_StringHash(&d->sites, hashKey);
    if (!params) {
        params = new_SiteParams();
        insert_StringHash(&d->sites, hashKey, params);
    }
    iBool needSave = iFalse;
    switch (key) {
        case titanPort_SiteSpecKey:
            params->titanPort = iClamp(value, 0, 0xffff);
            needSave = iTrue;
        default:
            break;
    }
    if (needSave) {
        save_SiteSpec_(d);
    }
}

int value_SiteSpec(const iString *site, enum iSiteSpecKey key) {
    iSiteSpec *d = &siteSpec_;
    const iSiteParams *params = constValue_StringHash(&d->sites, collect_String(lower_String(site)));
    if (!params) {
        return 0;
    }
    switch (key) {
        case titanPort_SiteSpecKey:
            return params->titanPort;
        default:
            return 0;
    }    
}
