#include "mimehooks.h"
#include "app.h"

#include <the_Foundation/file.h>
#include <the_Foundation/fileinfo.h>
#include <the_Foundation/path.h>
#include <the_Foundation/process.h>
#include <the_Foundation/stringlist.h>
#include <the_Foundation/xml.h>

iDefineTypeConstruction(FilterHook)

void init_FilterHook(iFilterHook *d) {
    init_String(&d->label);
    init_String(&d->mimePattern);
    init_String(&d->command);
    d->mimeRegex = NULL;
}

void deinit_FilterHook(iFilterHook *d) {
    iRelease(d->mimeRegex);
    deinit_String(&d->command);
    deinit_String(&d->mimePattern);
    deinit_String(&d->label);
}

void setMimePattern_FilterHook(iFilterHook *d, const iString *pattern) {
    iReleasePtr(&d->mimeRegex);
    set_String(&d->mimePattern, pattern);
    d->mimeRegex = new_RegExp(cstr_String(pattern), caseInsensitive_RegExpOption);
}

void setCommand_FilterHook(iFilterHook *d, const iString *command) {
    set_String(&d->command, command);
}

iBlock *run_FilterHook_(const iFilterHook *d, const iString *mime, const iBlock *body,
                        const iString *requestUrl) {
    iProcess *   proc = new_Process();
    iStringList *args = new_StringList();
    iRangecc     seg  = iNullRange;
    while (nextSplit_Rangecc(range_String(&d->command), ";", &seg)) {
        pushBackRange_StringList(args, seg);
    }
    seg = iNullRange;
    while (nextSplit_Rangecc(range_String(mime), ";", &seg)) {
        pushBackRange_StringList(args, seg);
    }
    setArguments_Process(proc, args);
    iRelease(args);
    if (!isEmpty_String(requestUrl)) {
        setEnvironment_Process(
            proc,
            iClob(newStrings_StringList(
                collectNewFormat_String("REQUEST_URL=%s", cstr_String(requestUrl)), NULL)));
    }
    iBlock *output = NULL;
    if (start_Process(proc)) {
        writeInput_Process(proc, body);
        output = readOutputUntilClosed_Process(proc);
        if (!startsWith_Rangecc(range_Block(output), "20")) {
            /* Didn't produce valid output. */
            delete_Block(output);
            output = NULL;
        }
    }
    iRelease(proc);
    return output;
}

/*----------------------------------------------------------------------------------------------*/

static iRegExp *xmlMimePattern_(void) {
    static iRegExp *xmlMime_;
    if (!xmlMime_) {
        xmlMime_ = new_RegExp("(application|text)/(atom\\+)?xml", caseInsensitive_RegExpOption);
    }
    return xmlMime_;
}

static iBlock *translateAtomXmlToGeminiFeed_(const iString *mime, const iBlock *source,
                                             const iString *requestUrl) {
    iRegExpMatch m;
    init_RegExpMatch(&m);
    if (!matchString_RegExp(xmlMimePattern_(), mime, &m)) {
        return NULL;
    }
    iBlock *      output = NULL;
    iXmlDocument *doc    = new_XmlDocument();
    iString       src;
    initBlock_String(&src, source); /* assume it's UTF-8 */
    if (!parse_XmlDocument(doc, &src)) {
        goto finished;
    }
    const iXmlElement *feed = &doc->root;
    if (!equal_Rangecc(feed->name, "feed")) {
        goto finished;
    }
    if (!equal_Rangecc(attribute_XmlElement(feed, "xmlns"), "http://www.w3.org/2005/Atom")) {
        goto finished;
    }
    iString *title = collect_String(decodedContent_XmlElement(child_XmlElement(feed, "title")));
    if (isEmpty_String(title)) {
        goto finished;
    }
    iString *subtitle = collect_String(decodedContent_XmlElement(child_XmlElement(feed, "subtitle")));
    iString out;
    init_String(&out);
    format_String(&out,
                  "20 text/gemini\r\n"
                  "# %s\n\n", cstr_String(title));
    if (!isEmpty_String(subtitle)) {
        appendFormat_String(&out, "## %s\n\n", cstr_String(subtitle));
    }
    iRegExp *datePattern =
        iClob(new_RegExp("^([0-9][0-9][0-9][0-9]-[0-1][0-9]-[0-3][0-9])T.*", caseSensitive_RegExpOption));
    iBeginCollect();
    iConstForEach(PtrArray, i, &feed->children) {
        iEndCollect();
        iBeginCollect();
        const iXmlElement *entry = i.ptr;
        if (!equal_Rangecc(entry->name, "entry")) {
            continue;
        }
        title = collect_String(decodedContent_XmlElement(child_XmlElement(entry, "title")));
        if (isEmpty_String(title)) {
            continue;
        }
        const iString *updated =
            collect_String(decodedContent_XmlElement(child_XmlElement(entry, "updated")));
        iRegExpMatch m;
        init_RegExpMatch(&m);
        if (!matchString_RegExp(datePattern, updated, &m)) {
            continue;
        }
        iRangecc url = iNullRange;
        iConstForEach(PtrArray, j, &entry->children) {
            const iXmlElement *link = j.ptr;
            if (!equal_Rangecc(link->name, "link")) {
                continue;
            }
            const iRangecc href = attribute_XmlElement(link, "href");
            const iRangecc rel = attribute_XmlElement(link, "rel");
            const iRangecc type = attribute_XmlElement(link, "type");
            if (startsWithCase_Rangecc(href, "gemini:")) {
                url = href;
                /* We're happy with the first gemini URL. */
                /* TODO: Are we? */
                break;
            }
            url = href;
            iUnused(rel, type);
        }
        if (isEmpty_Range(&url)) {
            continue;
        }
        appendFormat_String(&out, "=> %s %s - %s\n",
                            cstr_Rangecc(url),
                            cstr_Rangecc(capturedRange_RegExpMatch(&m, 1)),
                            cstr_String(title));
    }
    iEndCollect();
    appendCStr_String(&out,
                      "This Atom XML document has been automatically translated to a Gemini feed "
                      "to allow subscribing to it.\n");
    output = copy_Block(utf8_String(&out));
    deinit_String(&out);
finished:
    delete_XmlDocument(doc);
    deinit_String(&src);
    return output;
}

/*----------------------------------------------------------------------------------------------*/

struct Impl_MimeHooks {
    iPtrArray filters;
};

iDefineTypeConstruction(MimeHooks)

void init_MimeHooks(iMimeHooks *d) {
    init_PtrArray(&d->filters);
}

void deinit_MimeHooks(iMimeHooks *d) {
    iForEach(PtrArray, i, &d->filters) {
        delete_FilterHook(i.ptr);
    }
    deinit_PtrArray(&d->filters);
}

iBool willTryFilter_MimeHooks(const iMimeHooks *d, const iString *mime) {
    /* TODO: Combine this function with tryFilter_MimeHooks! */
    iRegExpMatch m;
    iConstForEach(PtrArray, i, &d->filters) {
        const iFilterHook *xc = i.ptr;
        init_RegExpMatch(&m);
        if (matchString_RegExp(xc->mimeRegex, mime, &m)) {
            return iTrue;
        }
    }
    /* Built-in filters. */
    init_RegExpMatch(&m);
    if (matchString_RegExp(xmlMimePattern_(), mime, &m)) {
        return iTrue;
    }
    return iFalse;
}

iBlock *tryFilter_MimeHooks(const iMimeHooks *d, const iString *mime, const iBlock *body,
                            const iString *requestUrl) {
    iRegExpMatch m;
    iConstForEach(PtrArray, i, &d->filters) {
        const iFilterHook *xc = i.ptr;
        init_RegExpMatch(&m);
        if (matchString_RegExp(xc->mimeRegex, mime, &m)) {
            iBlock *result = run_FilterHook_(xc, mime, body, requestUrl);
            if (result) {
                return result;
            }
        }
    }
    /* Built-in filters. */
    init_RegExpMatch(&m);
    if (matchString_RegExp(xmlMimePattern_(), mime, &m)) {
        iBlock *result = translateAtomXmlToGeminiFeed_(mime, body, requestUrl);
        if (result) {
            return result;
        }
    }
    return NULL;
}

static const char *mimeHooksFilename_MimeHooks_ = "mimehooks.txt";

void load_MimeHooks(iMimeHooks *d, const char *saveDir) {
    iBool reportError = iFalse;
    iFile *f = newCStr_File(concatPath_CStr(saveDir, mimeHooksFilename_MimeHooks_));
    if (open_File(f, read_FileMode | text_FileMode)) {
        iBlock * src     = readAll_File(f);
        iRangecc srcLine = iNullRange;
        int      pos     = 0;
        iRangecc lines[3];
        iZap(lines);
        while (nextSplit_Rangecc(range_Block(src), "\n", &srcLine)) {
            iRangecc line = srcLine;
            trim_Rangecc(&line);
            if (isEmpty_Range(&line)) {
                continue;
            }
            lines[pos++] = line;
            if (pos == 3) {
                iFilterHook *hook = new_FilterHook();
                setRange_String(&hook->label, lines[0]);
                setMimePattern_FilterHook(hook, collect_String(newRange_String(lines[1])));
                setCommand_FilterHook(hook, collect_String(newRange_String(lines[2])));
                /* Check if commmand is valid. */ {
                    iRangecc seg = iNullRange;
                    while (nextSplit_Rangecc(range_String(&hook->command), ";", &seg)) {
                        if (!fileExistsCStr_FileInfo(cstr_Rangecc(seg))) {
                            reportError = iTrue;
                        }
                        break;
                    }
                }
                pushBack_PtrArray(&d->filters, hook);
                pos = 0;
            }
        }
        delete_Block(src);
    }
    iRelease(f);
    if (reportError) {
        postCommand_App("~config.error where:mimehooks.txt");
    }
}

void save_MimeHooks(const iMimeHooks *d) {
    iUnused(d);
}

const iString *debugInfo_MimeHooks(const iMimeHooks *d) {
    iString *str = collectNew_String();
    size_t index = 0;
    iConstForEach(PtrArray, i, &d->filters) {
        const iFilterHook *filter = i.ptr;
        appendFormat_String(str, "### %d: %s\n", index, cstr_String(&filter->label));
        appendFormat_String(str, "MIME regex:\n```\n%s\n```\n", cstr_String(&filter->mimePattern));
        iStringList *args = iClob(split_String(&filter->command, ";"));
        if (isEmpty_StringList(args)) {
            appendFormat_String(str, "\u26a0 Command not specified!\n");
            continue;
        }
        const iString *exec = constAt_StringList(args, 0);
        if (isEmpty_String(exec)) {
            appendFormat_String(str, "\u26a0 Command not specified!\n");
        }
        else {
            appendFormat_String(str, "Executable: %s\n```\n%s\n```\n",
                                fileExists_FileInfo(exec) ? "" : "\u26a0 FILE NOT FOUND",
                                cstr_String(exec));
        }
        index++;
    }
    return str;
}
