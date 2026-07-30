#include "chatserver.hpp"
#include "AsyncLog.hpp"
#include "chatservice.hpp"
#include "ConfigFileReader.hpp"
#include "db.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/limits.h>
#include <signal.h>
#include <cstdio>
#include <iostream>
using namespace std;
using namespace muduo;
using namespace muduo::net;

EventLoop *g_loop = nullptr;

std::string getProjectRootDir()
{
  char path[PATH_MAX];
  int len = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (len > 0)
  {
    path[len] = '\0';
    std::string exePath(path);
    std::string exeDir = exePath.substr(0, exePath.find_last_of('/'));
    return exeDir + "/..";
  }
  return ".";
}

void resetHandler(int signo)
{
  LOGI("program recv signal[%d] to exit.", signo);
  if (g_loop)
    g_loop->quit();
}

int main(int argc, char *argv[])
{
  // 设置信号处理
  signal(SIGCHLD, SIG_DFL);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, resetHandler);
  signal(SIGTERM, resetHandler);

  std::string projectRoot = getProjectRootDir();
  CConfigFileReader config((projectRoot + "/etc/chatserver.conf").c_str());

  std::string logFileFullPath;

  const char *logfilepath = config.getConfigName("logfiledir");
  if (logfilepath == NULL)
  {
    LOGF("logdir is not set in config file");
    return 1;
  }

  std::string absLogDir;
  std::string logDirStr(logfilepath);
  if (!logDirStr.empty() && logDirStr[0] != '/')
  {
    absLogDir = projectRoot + "/" + logDirStr;
  }
  else
  {
    absLogDir = logDirStr;
  }

  DIR *dp = opendir(absLogDir.c_str());
  if (dp == NULL)
  {
    if (mkdir(absLogDir.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0)
    {
      LOGF("create base dir error, %s , errno: %d, %s", absLogDir.c_str(), errno, strerror(errno));
      return 1;
    }
  }
  closedir(dp);

  logFileFullPath = absLogDir;

  const char *logfilename = config.getConfigName("logfilename");
  if (logfilename == NULL) logfilename = "chatserver";
  logFileFullPath += logfilename;

  CAsyncLog::init(logFileFullPath.c_str());

  const char *dbserver = config.getConfigName("dbserver");
  const char *dbuser = config.getConfigName("dbuser");
  const char *dbpassword = config.getConfigName("dbpassword");
  const char *dbname = config.getConfigName("dbname");
  if (dbserver == NULL || dbuser == NULL || dbpassword == NULL || dbname == NULL)
  {
    LOGF("mysql config is not set in config file");
    return 1;
  }
  MySQL::setConfig(dbserver, dbuser, dbpassword, dbname);

  EventLoop loop;
  g_loop = &loop;

  const char *serverip = config.getConfigName("serverip");
  if (serverip == NULL) { LOGF("serverip not set in config file"); return 1; }
  const char *portstr = config.getConfigName("serverport");
  if (portstr == NULL) { LOGF("serverport not set in config file"); return 1; }
  short serverport = (short)atol(portstr);

  if (argc == 3)
  {
    serverip = argv[1];
    serverport = (short)atol(argv[2]);
  }
  else if (argc != 1)
  {
    fprintf(stderr, "Usage: %s [ip port]\n", argv[0]);
    return 1;
  }

  InetAddress addr(serverip, serverport);

  ChatServer server(&loop, addr, "ChatServer");

  server.start();
  ChatService::instance()->startHeartbeatCheck(&loop);
  loop.loop();

  ChatService::instance()->reset();
  CAsyncLog::uninit();
  return 0;
}
