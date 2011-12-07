// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/file_util.h"
#include "base/path_service.h"
#include "base/test/test_timeouts.h"
#include "build/build_config.h"
#include "content/browser/plugin_service.h"
#include "content/public/common/content_switches.h"
#include "content/common/pepper_plugin_registry.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/automation/tab_proxy.h"
#include "chrome/test/base/ui_test_utils.h"
#include "chrome/test/ui/ui_test.h"
#include "net/base/net_util.h"
#include "net/test/test_server.h"
#include "webkit/plugins/plugin_switches.h"


namespace {

// Platform-specific filename relative to the chrome executable.
#if defined(OS_WIN)
const wchar_t library_name[] = L"ppapi_tests.dll";
#elif defined(OS_MACOSX)
const char library_name[] = "ppapi_tests.plugin";
#elif defined(OS_POSIX)
const char library_name[] = "libppapi_tests.so";
#endif

}  // namespace

class PPAPITestBase : public UITest {
 public:
  PPAPITestBase() {
    // The test sends us the result via a cookie.
    launch_arguments_.AppendSwitch(switches::kEnableFileCookies);

    // Some stuff is hung off of the testing interface which is not enabled
    // by default.
    launch_arguments_.AppendSwitch(switches::kEnablePepperTesting);

    // Smooth scrolling confuses the scrollbar test.
    launch_arguments_.AppendSwitch(switches::kDisableSmoothScrolling);
  }

  virtual std::string BuildQuery(const std::string& base,
                                 const std::string& test_case)=0;

  void RunTest(const std::string& test_case) {
    FilePath test_path;
    EXPECT_TRUE(PathService::Get(base::DIR_SOURCE_ROOT, &test_path));
    test_path = test_path.Append(FILE_PATH_LITERAL("ppapi"));
    test_path = test_path.Append(FILE_PATH_LITERAL("tests"));
    test_path = test_path.Append(FILE_PATH_LITERAL("test_case.html"));

    // Sanity check the file name.
    EXPECT_TRUE(file_util::PathExists(test_path));

    GURL::Replacements replacements;
    std::string query = BuildQuery("", test_case);
    replacements.SetQuery(query.c_str(), url_parse::Component(0, query.size()));
    GURL test_url = net::FilePathToFileURL(test_path);
    RunTestURL(test_url.ReplaceComponents(replacements));
  }

  void RunTestViaHTTP(const std::string& test_case) {
    // For HTTP tests, we use the output DIR to grab the generated files such
    // as the NEXEs.
    FilePath exe_dir = CommandLine::ForCurrentProcess()->GetProgram().DirName();
    FilePath src_dir;
    ASSERT_TRUE(PathService::Get(base::DIR_SOURCE_ROOT, &src_dir));

    // TestServer expects a path relative to source. So we must first
    // generate absolute paths to SRC and EXE and from there generate
    // a relative path.
    if (!exe_dir.IsAbsolute()) file_util::AbsolutePath(&exe_dir);
    if (!src_dir.IsAbsolute()) file_util::AbsolutePath(&src_dir);
    ASSERT_TRUE(exe_dir.IsAbsolute());
    ASSERT_TRUE(src_dir.IsAbsolute());

    size_t match, exe_size, src_size;
    std::vector<FilePath::StringType> src_parts, exe_parts;

    // Determine point at which src and exe diverge, and create a relative path.
    exe_dir.GetComponents(&exe_parts);
    src_dir.GetComponents(&src_parts);
    exe_size = exe_parts.size();
    src_size = src_parts.size();
    for (match = 0; match < exe_size && match < src_size; ++match) {
      if (exe_parts[match] != src_parts[match])
        break;
    }
    FilePath web_dir;
    for (size_t tmp_itr = match; tmp_itr < src_size; ++tmp_itr) {
      web_dir = web_dir.Append(FILE_PATH_LITERAL(".."));
    }
    for (; match < exe_size; ++match) {
      web_dir = web_dir.Append(exe_parts[match]);
    }

    net::TestServer test_server(net::TestServer::TYPE_HTTP, web_dir);
    ASSERT_TRUE(test_server.Start());
    std::string query = BuildQuery("files/test_case.html?", test_case);
    RunTestURL(test_server.GetURL(query));
  }

  void RunTestWithWebSocketServer(const std::string& test_case) {
    FilePath websocket_root_dir;
    ASSERT_TRUE(
        PathService::Get(chrome::DIR_LAYOUT_TESTS, &websocket_root_dir));

    ui_test_utils::TestWebSocketServer server;
    ASSERT_TRUE(server.Start(websocket_root_dir));
    RunTest(test_case);
  }

 private:
  void RunTestURL(const GURL& test_url) {
    scoped_refptr<TabProxy> tab(GetActiveTab());
    ASSERT_TRUE(tab.get());
    ASSERT_TRUE(tab->NavigateToURL(test_url));

    // See comment above TestingInstance in ppapi/test/testing_instance.h.
    // Basically it sets a series of numbered cookies. The value of "..." means
    // it's still working and we should continue to wait, any other value
    // indicates completion (in this case it will start with "PASS" or "FAIL").
    // This keeps us from timing out on cookie waits for long tests.
    int progress_number = 0;
    std::string progress;
    while (true) {
      std::string cookie_name = StringPrintf("PPAPI_PROGRESS_%d",
                                             progress_number);
      progress = WaitUntilCookieNonEmpty(tab.get(), test_url,
            cookie_name.c_str(), TestTimeouts::large_test_timeout_ms());
      if (progress != "...")
        break;
      progress_number++;
    }

    if (progress_number == 0) {
      // Failing the first time probably means the plugin wasn't loaded.
      ASSERT_FALSE(progress.empty())
          << "Plugin couldn't be loaded. Make sure the PPAPI test plugin is "
          << "built, in the right place, and doesn't have any missing symbols.";
    } else {
      ASSERT_FALSE(progress.empty()) << "Test timed out.";
    }

    EXPECT_STREQ("PASS", progress.c_str());
  }
};

// In-process plugin test runner.  See OutOfProcessPPAPITest below for the
// out-of-process version.
class PPAPITest : public PPAPITestBase {
 public:
  PPAPITest() {
    // Append the switch to register the pepper plugin.
    // library name = <out dir>/<test_name>.<library_extension>
    // MIME type = application/x-ppapi-<test_name>
    FilePath plugin_dir;
    EXPECT_TRUE(PathService::Get(base::DIR_EXE, &plugin_dir));

    FilePath plugin_lib = plugin_dir.Append(library_name);
    EXPECT_TRUE(file_util::PathExists(plugin_lib));
    FilePath::StringType pepper_plugin = plugin_lib.value();
    pepper_plugin.append(FILE_PATH_LITERAL(";application/x-ppapi-tests"));
    launch_arguments_.AppendSwitchNative(switches::kRegisterPepperPlugins,
                                         pepper_plugin);
    launch_arguments_.AppendSwitchASCII(switches::kAllowNaClSocketAPI,
                                        "127.0.0.1");
  }

  std::string BuildQuery(const std::string& base,
                         const std::string& test_case){
    return StringPrintf("%stestcase=%s", base.c_str(), test_case.c_str());
  }

};

// Variant of PPAPITest that runs plugins out-of-process to test proxy
// codepaths.
class OutOfProcessPPAPITest : public PPAPITest {
 public:
  OutOfProcessPPAPITest() {
    // Run PPAPI out-of-process to exercise proxy implementations.
    launch_arguments_.AppendSwitch(switches::kPpapiOutOfProcess);
  }
};

// NaCl plugin test runner.
class PPAPINaClTest : public PPAPITestBase {
 public:
  PPAPINaClTest() {
    FilePath plugin_lib;
    EXPECT_TRUE(PathService::Get(chrome::FILE_NACL_PLUGIN, &plugin_lib));
    EXPECT_TRUE(file_util::PathExists(plugin_lib));

    // Enable running NaCl outside of the store.
    launch_arguments_.AppendSwitch(switches::kEnableNaCl);
    launch_arguments_.AppendSwitchASCII(switches::kAllowNaClSocketAPI,
                                        "127.0.0.1");
  }

  // Append the correct mode and testcase string
  std::string BuildQuery(const std::string& base,
                         const std::string& test_case) {
    return StringPrintf("%smode=nacl&testcase=%s", base.c_str(),
                        test_case.c_str());
  }
};


// Use these macros to run the tests for a specific interface.
// Most interfaces should be tested with both macros.
#define TEST_PPAPI_IN_PROCESS(test_name) \
    TEST_F(PPAPITest, test_name) { \
      RunTest(#test_name); \
    }
#define TEST_PPAPI_OUT_OF_PROCESS(test_name) \
    TEST_F(OutOfProcessPPAPITest, test_name) { \
      RunTest(#test_name); \
    }

// Similar macros that test over HTTP.
#define TEST_PPAPI_IN_PROCESS_VIA_HTTP(test_name) \
    TEST_F(PPAPITest, test_name) { \
      RunTestViaHTTP(#test_name); \
    }
#define TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(test_name) \
    TEST_F(OutOfProcessPPAPITest, test_name) { \
      RunTestViaHTTP(#test_name); \
    }

// Similar macros that test with WebSocket server
#define TEST_PPAPI_IN_PROCESS_WITH_WS(test_name) \
    TEST_F(PPAPITest, test_name) { \
      RunTestWithWebSocketServer(#test_name); \
    }
#define TEST_PPAPI_OUT_OF_PROCESS_WITH_WS(test_name) \
    TEST_F(OutOfProcessPPAPITest, test_name) { \
      RunTestWithWebSocketServer(#test_name); \
    }


#if defined(DISABLE_NACL)
#define TEST_PPAPI_NACL_VIA_HTTP(test_name)
#else

// NaCl based PPAPI tests
#define TEST_PPAPI_NACL_VIA_HTTP(test_name) \
    TEST_F(PPAPINaClTest, test_name) { \
  RunTestViaHTTP(#test_name); \
}
#endif


//
// Interface tests.
//

// Disable tests under ASAN.  http://crbug.com/104832.
// This is a bit heavy handed, but the majority of these tests fail under ASAN.
// See bug for history.
#if !defined(ADDRESS_SANITIZER)

TEST_PPAPI_IN_PROCESS(Broker)
TEST_PPAPI_OUT_OF_PROCESS(Broker)

TEST_PPAPI_IN_PROCESS(Core)
TEST_PPAPI_OUT_OF_PROCESS(Core)

TEST_PPAPI_IN_PROCESS(CursorControl)
TEST_PPAPI_OUT_OF_PROCESS(CursorControl)
TEST_PPAPI_NACL_VIA_HTTP(CursorControl)

TEST_PPAPI_IN_PROCESS(InputEvent)
TEST_PPAPI_OUT_OF_PROCESS(InputEvent)

TEST_PPAPI_IN_PROCESS(Instance)
// http://crbug.com/91729
TEST_PPAPI_OUT_OF_PROCESS(DISABLED_Instance)

TEST_PPAPI_IN_PROCESS(Graphics2D)
TEST_PPAPI_OUT_OF_PROCESS(Graphics2D)
TEST_PPAPI_NACL_VIA_HTTP(Graphics2D)

TEST_PPAPI_IN_PROCESS(ImageData)
TEST_PPAPI_OUT_OF_PROCESS(ImageData)
TEST_PPAPI_NACL_VIA_HTTP(ImageData)

TEST_PPAPI_IN_PROCESS(Buffer)
TEST_PPAPI_OUT_OF_PROCESS(Buffer)

// TODO(ygorshenin): investigate why
// TEST_PPAPI_IN_PROCESS(TCPSocketPrivateShared) fails,
// http://crbug.com/105860.
TEST_PPAPI_IN_PROCESS_VIA_HTTP(TCPSocketPrivateShared)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(TCPSocketPrivateShared)
TEST_PPAPI_NACL_VIA_HTTP(TCPSocketPrivateShared)

// TODO(ygorshenin): investigate why
// TEST_PPAPI_IN_PROCESS(UDPSocketPrivateShared) fails,
// http://crbug.com/105860.
TEST_PPAPI_IN_PROCESS_VIA_HTTP(UDPSocketPrivateShared)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(UDPSocketPrivateShared)
TEST_PPAPI_NACL_VIA_HTTP(UDPSocketPrivateShared)

TEST_PPAPI_IN_PROCESS_VIA_HTTP(URLLoader)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(URLLoader)
TEST_PPAPI_NACL_VIA_HTTP(URLLoader)

TEST_PPAPI_IN_PROCESS(PaintAggregator)
TEST_PPAPI_OUT_OF_PROCESS(PaintAggregator)
TEST_PPAPI_NACL_VIA_HTTP(PaintAggregator)

TEST_PPAPI_IN_PROCESS(Scrollbar)
// http://crbug.com/89961
TEST_F(OutOfProcessPPAPITest, FAILS_Scrollbar) {
  RunTest("Scrollbar");
}
TEST_PPAPI_NACL_VIA_HTTP(Scrollbar)

TEST_PPAPI_IN_PROCESS(URLUtil)
TEST_PPAPI_OUT_OF_PROCESS(URLUtil)

TEST_PPAPI_IN_PROCESS(CharSet)
TEST_PPAPI_OUT_OF_PROCESS(CharSet)

TEST_PPAPI_IN_PROCESS(Crypto)
TEST_PPAPI_OUT_OF_PROCESS(Crypto)

TEST_PPAPI_IN_PROCESS(Var)
TEST_PPAPI_OUT_OF_PROCESS(Var)
TEST_PPAPI_NACL_VIA_HTTP(Var)

TEST_PPAPI_IN_PROCESS(VarDeprecated)
// Disabled because it times out: http://crbug.com/89961
//TEST_PPAPI_OUT_OF_PROCESS(VarDeprecated)

// Windows defines 'PostMessage', so we have to undef it.
#ifdef PostMessage
#undef PostMessage
#endif
TEST_PPAPI_IN_PROCESS(PostMessage_SendInInit)
TEST_PPAPI_IN_PROCESS(PostMessage_SendingData)
TEST_PPAPI_IN_PROCESS(PostMessage_MessageEvent)
TEST_PPAPI_IN_PROCESS(PostMessage_NoHandler)
TEST_PPAPI_IN_PROCESS(PostMessage_ExtraParam)
TEST_PPAPI_OUT_OF_PROCESS(PostMessage_SendInInit)
TEST_PPAPI_OUT_OF_PROCESS(PostMessage_SendingData)
TEST_PPAPI_OUT_OF_PROCESS(PostMessage_MessageEvent)
TEST_PPAPI_OUT_OF_PROCESS(PostMessage_NoHandler)
TEST_PPAPI_OUT_OF_PROCESS(PostMessage_ExtraParam)
#if !defined(OS_WIN)
// Times out on Windows XP: http://crbug.com/95557
TEST_PPAPI_OUT_OF_PROCESS(PostMessage_NonMainThread)
#endif

TEST_PPAPI_IN_PROCESS(Memory)
TEST_PPAPI_OUT_OF_PROCESS(Memory)
TEST_PPAPI_NACL_VIA_HTTP(Memory)

TEST_PPAPI_IN_PROCESS(VideoDecoder)
TEST_PPAPI_OUT_OF_PROCESS(VideoDecoder)

// http://crbug.com/90039 and http://crbug.com/83443 (Mac)
TEST_F(PPAPITest, FAILS_FileIO) {
  RunTestViaHTTP("FileIO");
}
// http://crbug.com/101154
TEST_F(OutOfProcessPPAPITest, DISABLED_FileIO) {
  RunTestViaHTTP("FileIO");
}
TEST_PPAPI_NACL_VIA_HTTP(DISABLED_FileIO)


TEST_PPAPI_IN_PROCESS_VIA_HTTP(FileRef)
// Disabled because it times out: http://crbug.com/89961
//TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(FileRef)
TEST_PPAPI_NACL_VIA_HTTP(FileRef)


TEST_PPAPI_IN_PROCESS_VIA_HTTP(FileSystem)
TEST_PPAPI_OUT_OF_PROCESS_VIA_HTTP(FileSystem)
TEST_PPAPI_NACL_VIA_HTTP(FileSystem)

// http://crbug.com/96767 and 104384 for aura.
#if !defined(OS_MACOSX) && !defined(USE_AURA)
#define MAYBE_FlashFullscreen FLAKY_FlashFullscreen
#define MAYBE_FlashFullscreen FLAKY_FlashFullscreen
#else
#define MAYBE_FlashFullscreen DISABLED_FlashFullscreen
#define MAYBE_FlashFullscreen DISABLED_FlashFullscreen
#endif

TEST_F(PPAPITest, MAYBE_FlashFullscreen) {
  RunTestViaHTTP("FlashFullscreen");
}
TEST_F(OutOfProcessPPAPITest, MAYBE_FlashFullscreen) {
  RunTestViaHTTP("FlashFullscreen");
}
// New implementation only honors fullscreen requests within a context of
// a user gesture. Since we do not yet have an infrastructure for testing
// those under ppapi_tests, the tests below time out when run automtically.
// To test the code, run them manually following the directions here:
//   www.chromium.org/developers/design-documents/pepper-plugin-implementation
// and click on the plugin area (gray square) to force fullscreen mode and
// get the test unstuck.
TEST_F(PPAPITest, DISABLED_Fullscreen) {
  RunTestViaHTTP("Fullscreen");
}
TEST_F(OutOfProcessPPAPITest, DISABLED_Fullscreen) {
  RunTestViaHTTP("Fullscreen");
}

TEST_PPAPI_IN_PROCESS(FlashClipboard)
TEST_PPAPI_OUT_OF_PROCESS(FlashClipboard)

#if defined(OS_POSIX)
#define MAYBE_DirectoryReader FLAKY_DirectoryReader
#else
#define MAYBE_DirectoryReader DirectoryReader
#endif

// Flaky on Mac + Linux, maybe http://codereview.chromium.org/7094008
// Not implemented out of process: http://crbug.com/106129
TEST_F(PPAPITest, MAYBE_DirectoryReader) {
  RunTestViaHTTP("DirectoryReader");
}

#if defined(ENABLE_P2P_APIS)
// Flaky. http://crbug.com/84294
TEST_F(PPAPITest, FLAKY_Transport) {
  RunTest("Transport");
}
// http://crbug.com/89961
TEST_F(OutOfProcessPPAPITest, FAILS_Transport) {
  RunTestViaHTTP("Transport");
}
#endif // ENABLE_P2P_APIS

// There is no proxy. This is used for PDF metrics reporting, and PDF only
// runs in process, so there's currently no need for a proxy.
TEST_PPAPI_IN_PROCESS(UMA)

TEST_PPAPI_IN_PROCESS(NetAddressPrivate_AreEqual)
TEST_PPAPI_IN_PROCESS(NetAddressPrivate_AreHostsEqual)
TEST_PPAPI_IN_PROCESS(NetAddressPrivate_Describe)
TEST_PPAPI_IN_PROCESS(NetAddressPrivate_ReplacePort)
TEST_PPAPI_IN_PROCESS(NetAddressPrivate_GetAnyAddress)
TEST_PPAPI_IN_PROCESS(NetAddressPrivate_DescribeIPv6)
TEST_PPAPI_OUT_OF_PROCESS(NetAddressPrivate_AreEqual)
TEST_PPAPI_OUT_OF_PROCESS(NetAddressPrivate_AreHostsEqual)
TEST_PPAPI_OUT_OF_PROCESS(NetAddressPrivate_Describe)
TEST_PPAPI_OUT_OF_PROCESS(NetAddressPrivate_ReplacePort)
TEST_PPAPI_OUT_OF_PROCESS(NetAddressPrivate_GetAnyAddress)
TEST_PPAPI_OUT_OF_PROCESS(NetAddressPrivate_DescribeIPv6)

// PPB_TCPSocket_Private currently isn't supported in-process.
TEST_F(OutOfProcessPPAPITest, TCPSocketPrivate) {
  RunTestViaHTTP("TCPSocketPrivate");
}

TEST_PPAPI_IN_PROCESS(Flash_SetInstanceAlwaysOnTop)
TEST_PPAPI_IN_PROCESS(Flash_GetProxyForURL)
TEST_PPAPI_IN_PROCESS(Flash_MessageLoop)
TEST_PPAPI_IN_PROCESS(Flash_GetLocalTimeZoneOffset)
TEST_PPAPI_IN_PROCESS(Flash_GetCommandLineArgs)
TEST_PPAPI_OUT_OF_PROCESS(Flash_SetInstanceAlwaysOnTop)
TEST_PPAPI_OUT_OF_PROCESS(Flash_GetProxyForURL)
TEST_PPAPI_OUT_OF_PROCESS(Flash_MessageLoop)
TEST_PPAPI_OUT_OF_PROCESS(Flash_GetLocalTimeZoneOffset)
TEST_PPAPI_OUT_OF_PROCESS(Flash_GetCommandLineArgs)

TEST_PPAPI_IN_PROCESS(WebSocket_IsWebSocket)
TEST_PPAPI_IN_PROCESS(WebSocket_InvalidConnect)
TEST_PPAPI_IN_PROCESS(WebSocket_GetURL)
TEST_PPAPI_IN_PROCESS_WITH_WS(WebSocket_ValidConnect)
TEST_PPAPI_IN_PROCESS_WITH_WS(WebSocket_GetProtocol)
TEST_PPAPI_IN_PROCESS_WITH_WS(WebSocket_TextSendReceive)

TEST_PPAPI_IN_PROCESS(AudioConfig_ValidConfigs)
TEST_PPAPI_IN_PROCESS(AudioConfig_InvalidConfigs)
TEST_PPAPI_OUT_OF_PROCESS(AudioConfig_ValidConfigs)
TEST_PPAPI_OUT_OF_PROCESS(AudioConfig_InvalidConfigs)

TEST_PPAPI_IN_PROCESS(Audio_Creation)
TEST_PPAPI_IN_PROCESS(Audio_DestroyNoStop)
TEST_PPAPI_IN_PROCESS(Audio_Failures)
TEST_PPAPI_OUT_OF_PROCESS(Audio_Creation)
TEST_PPAPI_OUT_OF_PROCESS(Audio_DestroyNoStop)
TEST_PPAPI_OUT_OF_PROCESS(Audio_Failures)

#endif // ADDRESS_SANITIZER
