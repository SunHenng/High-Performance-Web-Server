#include "log/Logger.hpp"
#include "log/AsynLogging.hpp"

#include <iostream>
#include <string>
#include <thread>
using namespace std;

const int n = 1000;


logging::AsynLogging asynLog("ModernWebServer",1024*128,3);

void writeFile(const std::string &msg)
{
    asynLog.append(msg);
}

void flushFile()
{
    asynLog.flush();
}

void funa()
{
    for(int i = 0;i<n;++i)
    {
        LOG_TRACE<<"funa "<<i;
    }
}

void funb()
{
    for(int i = 0;i<n;++i)
    {
        LOG_DEBUG<<"funb "<<i;
    }
}

void func()
{
    for(int i = 0;i<n;++i)
    {
        LOG_INFO<<"func "<<i;
    }
}
int main()
{

   logging::Logger::SetLogLevel(logging::LOG_LEVEL::TRACE);
   logging::Logger::SetOuput(writeFile);
   logging::Logger::SetFlush(flushFile);
   asynLog.start();



   std::thread tha(funa);
   std::thread thb(funb);
   std::thread thc(func);

   tha.join();
   thb.join();
   thc.join();

   return 0;
}