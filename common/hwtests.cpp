#include "common/hwtests.h"

struct TestStatus
{
  TestStatus(const char* file, int line)
      : num_passes(0), num_failures(0), num_subtests(0), file(file), line(line)
  {
  }

  long long num_passes;
  long long num_failures;
  long long num_subtests;

  const char* file;
  int line;
};

static TestStatus status(NULL, 0);
static int number_of_tests = 0;
static long long number_of_subtests;
static int number_of_tests_passed = 0;
static long long number_of_subtests_passed;

int client_socket;
int server_socket;

// This is for Dolphin's benefit (with OSREPORT HLE). It won't end up on screen.
// __attribute__((weak)) is needed so that it doesn't get optimized out.
extern "C" __attribute__((weak)) void OSReport(const char* fmt, ...) {}

void network_vprintf(const char* str, va_list args)
{
  char buffer[4096];
  //	int len = vsnprintf(buffer, 4096, str, args);
  int len = vsprintf(buffer, str, args);
  // NOTE: vsprintf's return value doesn't include the null terminator.
  // But we don't want to send the null terminator over the network either.
  net_send(client_socket, buffer, len, 0);
  OSReport("%s", buffer);
}

void network_printf(const char* str, ...)
{
  va_list args;
  va_start(args, str);
  network_vprintf(str, args);
  va_end(args);
}

void privStartTest(const char* file, int line)
{
  status = TestStatus(file, line);

  number_of_tests++;
}

void privDoTest(bool condition, const char* file, int line, const char* fail_msg, ...)
{
  va_list arglist;
  va_start(arglist, fail_msg);

  ++status.num_subtests;

  if (condition)
  {
    ++status.num_passes;
  }
  else
  {
    ++status.num_failures;

    // TODO: vprintf forwarding doesn't seem to work?
    network_printf("Subtest %lld failed in %s on line %d: ", status.num_subtests, file, line);
    network_vprintf(fail_msg, arglist);
    network_printf("\n");
  }
  va_end(arglist);
}

void privEndTest()
{
  if (0 == status.num_failures)
  {
    network_printf("Test %d passed (%lld subtests)\n", number_of_tests, status.num_subtests);
    number_of_tests_passed++;
  }
  else
  {
    network_printf("Test %d failed (%lld subtests, %lld failures)\n", number_of_tests,
                   status.num_subtests, status.num_failures);
  }
  number_of_subtests += status.num_subtests;
  number_of_subtests_passed += status.num_passes;
}

void privSimpleTest(bool condition, const char* file, int line, const char* fail_msg, ...)
{
  // TODO
}

void report_test_results()
{
  network_printf("%d tests passed out of %d; %lld subtests passed out of %lld\n",
                 number_of_tests_passed, number_of_tests, number_of_subtests_passed, number_of_subtests);
}

#define SERVER_PORT 16784

void network_init()
{
  struct sockaddr_in my_name;

  my_name.sin_family = AF_INET;
  my_name.sin_port = htons(SERVER_PORT);
  my_name.sin_addr.s_addr = htonl(INADDR_ANY);

  net_init();

  server_socket = net_socket(AF_INET, SOCK_STREAM, 0);
  int yes = 1;
  net_setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  while (net_bind(server_socket, (struct sockaddr*)&my_name, sizeof(my_name)) < 0)
  {
  }

  net_listen(server_socket, 0);

  struct sockaddr_in client_info;
  socklen_t ssize = sizeof(client_info);
  client_socket = net_accept(server_socket, (struct sockaddr*)&client_info, &ssize);

  network_printf("Hello world!\n");
}

void network_shutdown()
{
  net_close(client_socket);
  net_close(server_socket);
}
