// Crash diagnostics (src/common/crash.*, docs/DEVELOPMENT.md "Crashes"): the pure helpers, the breadcrumb trail, the guarded() fault counter and,
// end to end, the banner and the minidump of a fault outside guarded() (the test catches the fault itself, the vectored handler still reports it).

TEST(crash_dump_kind_parsing) {
    using spass::crash::DumpKind;
    using spass::crash::parseDumpKind;
    CHECK(parseDumpKind("") == DumpKind::Off);
    CHECK(parseDumpKind("off") == DumpKind::Off);
    CHECK(parseDumpKind("false") == DumpKind::Off);
    CHECK(parseDumpKind("nonsense") == DumpKind::Off);
    CHECK(parseDumpKind("mini") == DumpKind::Mini);
    CHECK(parseDumpKind("true") == DumpKind::Mini);
    CHECK(parseDumpKind("full") == DumpKind::Full);
}

TEST(crash_exception_names_and_address_formatting) {
    CHECK(std::string(spass::crash::exceptionName(0xC0000005)) == "ACCESS_VIOLATION");
    CHECK(std::string(spass::crash::exceptionName(0xC0000374)) == "HEAP_CORRUPTION");
    CHECK(std::string(spass::crash::exceptionName(0xC0000409)).find("fast fail") != std::string::npos);
    CHECK(std::string(spass::crash::exceptionName(0x12345678)).empty());
    CHECK(spass::crash::formatModuleOffset("Game.dll", 0x805587) == "Game.dll+0x805587");
    CHECK(spass::crash::formatModuleOffset("", 0x10) == "0x10");
    CHECK(spass::crash::baseName("B:\\DevDrive\\git\\src\\game\\lua_template.cpp") == "lua_template.cpp");
    CHECK(spass::crash::baseName("src/game/abi.h") == "abi.h");
    CHECK(spass::crash::baseName("plain.cpp") == "plain.cpp");
}

TEST(crash_breadcrumbs_nest_and_unwind) {
    CHECK(spass::crash::breadcrumbs().empty());
    {
        spass::crash::Scope a("session.start", "load");
        CHECK(spass::crash::breadcrumbs() == "session.start load");
        {
            spass::crash::Scope b("tick", "balance_example");
            spass::game::guarded([] {});
            CHECK(spass::crash::breadcrumbs() == "session.start load > tick balance_example");
        }
        CHECK(spass::crash::breadcrumbs() == "session.start load");
    }
    CHECK(spass::crash::breadcrumbs().empty());
    // more levels than the trail stores: the surplus is counted, the unwinding stays balanced
    {
        std::vector<std::unique_ptr<spass::crash::Scope>> many;
        for (int i = 0; i < 40; ++i) many.push_back(std::make_unique<spass::crash::Scope>("level"));
        CHECK(spass::crash::breadcrumbs().find("(+24 deeper)") != std::string::npos);
    }
    CHECK(spass::crash::breadcrumbs().empty());
}

TEST(crash_guarded_notes_its_call_site_and_counts_faults) {
    const auto before = spass::crash::guardedFaultCount();
    std::string seen;
    const bool ok = spass::game::guarded([&] {
        seen = spass::crash::breadcrumbs();
        *reinterpret_cast<volatile int*>(0x10) = 1;  // access violation, caught by guarded()
    });
    CHECK(!ok);
    CHECK(seen.find("guarded crash_tests.inc:") != std::string::npos);
    CHECK(spass::crash::guardedFaultCount() == before + 1);
    CHECK(spass::crash::breadcrumbs().empty());
    CHECK(spass::game::guarded([] {}));
    CHECK(spass::crash::guardedFaultCount() == before + 1);
}

static void faultAndCatch() {
    __try {
        *reinterpret_cast<volatile int*>(0x18) = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

TEST(crash_banner_and_minidump_of_a_fault_outside_guarded) {
    TempDir dir;
    spass::crash::Options options;
    options.dump = spass::crash::DumpKind::Mini;
    options.dumpDir = dir.path / "crash";
    options.logFile = dir.path / "spass.log";
    spass::crash::install(options);
    {
        spass::crash::Scope scope("session.start", "load");
        faultAndCatch();
    }
    std::string banner;
    {
        std::ifstream in(dir.path / "crash.log", std::ios::binary);
        banner.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    CHECK(banner.find("CRASH (first chance, outside guarded())") != std::string::npos);
    CHECK(banner.find("ACCESS_VIOLATION (0xC0000005): write of 0x18") != std::string::npos);
    CHECK(banner.find("sentry.dll[layer]+0x") != std::string::npos);  // the test exe contains the "layer"
    CHECK(banner.find("layer was doing: session.start load") != std::string::npos);
    CHECK(banner.find("registers: rip=") != std::string::npos);
    CHECK(banner.find("stack scan") != std::string::npos);
    CHECK(banner.find("verdict:") != std::string::npos);
    bool dump = false;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir.path / "crash", ec)) {
        dump = dump || (e.path().extension() == ".dmp" && fs::file_size(e.path()) > 1000);
    }
    CHECK(dump);
}
