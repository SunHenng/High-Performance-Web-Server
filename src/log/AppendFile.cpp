// C api
#include <cassert>
#include <string.h>
#include <error.h>
// OWNER
#include "log/AppendFile.hpp"

#include<iostream>
using namespace std;

namespace logging
{
    // class AppendFile
    // static const size_t FILE_BUFF_SIZE = 128 * 1024; // 128k;
    // std::unique_ptr<char[]> buffer_;
    // FILE *fp_;
    // size_t writenBytes_;

    size_t AppendFile::write(const char *msg, const size_t len)
    {
        // return fwrite(msg, sizeof(char), len, fp_);
        return ::fwrite_unlocked(msg, sizeof(char), len, fp_);
    }
    AppendFile::AppendFile(const std::string &filename)
        : fp_{nullptr},
          buffer_{new char[FILE_BUFF_SIZE]},
          writenBytes_{0}
    {
        // cout<<filename<<endl;
        fp_ = fopen(filename.c_str(), "a"); // "w";
        // 4k
        assert(fp_ != nullptr);
        setbuffer(fp_, buffer_.get(), FILE_BUFF_SIZE);
    }
    AppendFile::~AppendFile()
    {
        fclose(fp_);
        fp_ = nullptr;
        buffer_.reset();
    }
    void AppendFile::append(const std::string &msg)
    {
        append(msg.c_str(), msg.size());
    }
    void AppendFile::append(const char *msg, const size_t len)
    {
        size_t n = write(msg, len); // len 1000
        size_t remain = len - n;
        while (remain > 0)
        {
            size_t x = write(msg + n, remain);
            if (x == 0)
            {
                int err = ferror(fp_);
                if (err)
                {
                    fprintf(stderr, "appendFile::append() failed %s \n",
                            strerror(err));
                    break;
                }
            }
            n += x;
            remain = len - n;
        }
        writenBytes_ += n;
    }
    void AppendFile::flush()
    {
        fflush(fp_);
    }
    size_t AppendFile::getWriteBytes() const
    {
        return writenBytes_;
    }
}