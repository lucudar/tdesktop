'''
This file is part of Telewhite, a fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

Batch 37: a pin confirmed by the server keeps its place in the list, and
the crash report tells where it crashed: the exception and the stack of
the crashed thread as module+offset pairs go to the report, the report of
the previous launch goes to log.txt and to tdata/last_crash.txt. See
telewhite_patch.py for the format.
'''
import os
import sys

import telewhite_patch
import telewhite_patch_more
import telewhite_patch_batch32
import telewhite_patch_batch33
import telewhite_patch_batch34
import telewhite_patch_batch35
import telewhite_patch_batch36

HISTORY = 'Telegram/SourceFiles/history/history.cpp'
CRASH = 'Telegram/SourceFiles/core/crash_reports.cpp'

FEATURES = [
    {
        'id': 'pins-keep-place',
        'title': 'pins: a pin confirmed by the server keeps its place',
        'pieces': [
            {
                'file': HISTORY,
                'find': r'''	owner().setChatPinned(this, FilterId(), data.is_pinned());
}''',
                'replace': r'''	// Telewhite: a pin the server confirms keeps its place in the list,
	// setting it again would move the chat to the top.
	if (data.is_pinned() && isPinnedDialog(FilterId())) {
		return;
	}
	owner().setChatPinned(this, FilterId(), data.is_pinned());
}''',
            },
        ],
    },
    {
        'id': 'crash-details',
        'title': 'crash report: the exception, the stack, the report in the log',
        'pieces': [
            {
                'file': CRASH,
                'find': r'''google_breakpad::ExceptionHandler* BreakpadExceptionHandler = 0;

#ifdef Q_OS_WIN
bool DumpCallback(''',
                'replace': r'''google_breakpad::ExceptionHandler* BreakpadExceptionHandler = 0;

#if defined Q_OS_WIN && defined _M_X64
// Telewhite: the report gets the exception and the stack of the crashed
// thread as module+offset pairs, to find the place by the report alone.
struct TelewhiteFrame {
	DWORD64 address = 0;
	DWORD64 base = 0;
};

constexpr auto kTelewhiteFrames = 48;

// Only plain data here: the stack may be broken, a fault is caught.
int TelewhiteCollectStack(
		const CONTEXT *from,
		TelewhiteFrame *frames,
		int limit) {
	auto count = 0;
	__try {
		CONTEXT context = *from;
		while (count < limit && context.Rip) {
			const auto pc = DWORD64(context.Rip);
			const auto sp = DWORD64(context.Rsp);
			DWORD64 base = 0;
			const auto entry = RtlLookupFunctionEntry(pc, &base, nullptr);
			frames[count].address = pc;
			frames[count].base = base;
			++count;
			if (entry) {
				PVOID handlerData = nullptr;
				DWORD64 establisherFrame = 0;
				RtlVirtualUnwind(
					UNW_FLAG_NHANDLER,
					base,
					pc,
					entry,
					&context,
					&handlerData,
					&establisherFrame,
					nullptr);
			} else {
				context.Rip = *reinterpret_cast<const DWORD64*>(sp);
				context.Rsp = sp + 8;
			}
			if (DWORD64(context.Rip) == pc && DWORD64(context.Rsp) == sp) {
				break;
			}
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
	return count;
}

void TelewhiteWriteHex(DWORD64 value) {
	char buffer[19] = { '0', 'x' };
	auto digits = 0;
	for (auto shift = 60; shift >= 0; shift -= 4) {
		const auto digit = int((value >> shift) & 0x0F);
		if (digits || digit || !shift) {
			buffer[2 + digits++] = (digit < 10)
				? char('0' + digit)
				: char('a' + (digit - 10));
		}
	}
	buffer[2 + digits] = 0;
	dump() << buffer;
}

void TelewhiteWriteModule(DWORD64 address, DWORD64 base) {
	auto module = reinterpret_cast<HMODULE>(static_cast<uintptr_t>(base));
	if (!module && !GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
				| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(static_cast<uintptr_t>(address)),
			&module)) {
		module = nullptr;
	}
	wchar_t path[MAX_PATH + 1] = { 0 };
	if (module && GetModuleFileNameW(module, path, MAX_PATH) > 0) {
		const wchar_t *name = path;
		for (auto i = path; *i; ++i) {
			if (*i == L'\\' || *i == L'/') {
				name = i + 1;
			}
		}
		dump() << name << "+";
		TelewhiteWriteHex(address - reinterpret_cast<DWORD64>(module));
	} else {
		TelewhiteWriteHex(address);
	}
}

void TelewhiteWriteCrashDetails(EXCEPTION_POINTERS *exinfo) {
	if (!ReportFile || !exinfo) {
		return;
	}
	if (const auto record = exinfo->ExceptionRecord) {
		dump() << "Exception: ";
		TelewhiteWriteHex(DWORD64(record->ExceptionCode));
		dump() << " at ";
		TelewhiteWriteModule(
			reinterpret_cast<DWORD64>(record->ExceptionAddress),
			0);
		if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
			&& record->NumberParameters >= 2) {
			const auto kind = record->ExceptionInformation[0];
			dump() << ((kind == 0)
				? ", reading "
				: (kind == 1)
				? ", writing "
				: ", executing ");
			TelewhiteWriteHex(DWORD64(record->ExceptionInformation[1]));
		}
		dump() << "\n";
	}
	if (const auto from = exinfo->ContextRecord) {
		TelewhiteFrame frames[kTelewhiteFrames];
		const auto count = TelewhiteCollectStack(
			from,
			frames,
			kTelewhiteFrames);
		dump() << "Stack:\n";
		for (auto i = 0; i != count; ++i) {
			dump() << "  ";
			TelewhiteWriteModule(frames[i].address, frames[i].base);
			dump() << "\n";
		}
	}
	dump() << "\n";
}
#endif // Q_OS_WIN && _M_X64

#ifdef Q_OS_WIN
bool DumpCallback(''',
            },
            {
                'file': CRASH,
                'find': r'''	BreakpadDumpPathW = _minidump_id;
	SignalHandler(-1);''',
                'replace': r'''	BreakpadDumpPathW = _minidump_id;
	SignalHandler(-1);
#ifdef _M_X64
	TelewhiteWriteCrashDetails(exinfo);
#endif // _M_X64''',
            },
            {
                'file': CRASH,
                'find': r'''			"Crash log size: %2").arg(ReportPath).arg(lastdump.size()));

		return lastdump;''',
                'replace': r'''			"Crash log size: %2").arg(ReportPath).arg(lastdump.size()));

		// Telewhite: the report goes to the log and to a file next to it,
		// the window about the crash shows it only once.
		if (!lastdump.isEmpty()) {
			auto lines = QStringList();
			const auto all = QString::fromUtf8(lastdump).split('\n');
			for (const auto &line : all) {
				if (!line.startsWith(u"Username:"_q)) {
					lines.push_back(line);
				}
			}
			const auto text = lines.join('\n');
			LOG(("Telewhite: the report of the previous launch:\n%1"
				).arg(text));
			auto copy = QFile(cWorkingDir() + u"tdata/last_crash.txt"_q);
			if (copy.open(QIODevice::WriteOnly)) {
				copy.write(text.toUtf8());
			}
		}

		return lastdump;''',
            },
        ],
    },
]

_known = {feature['id'] for feature in telewhite_patch.FEATURES}
telewhite_patch.FEATURES.extend(
    feature for feature in FEATURES if feature['id'] not in _known)


if __name__ == '__main__':
    arguments = sys.argv[1:]
    root = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    if '--root' in arguments:
        root = arguments[arguments.index('--root') + 1]
    ok = telewhite_patch.run(root, check=('--check' in arguments))
    sys.exit(0 if ok else 1)
