/****************************************************************************
 Copyright (c) 2016 Chukong Technologies Inc.
 Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.

 http://www.cocos2d-x.org

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/

#define LOG_TAG "AudioDecoderMp3"
#include "audio/include/AudioDecoderMp3.h"
#include "audio/include/AudioMacros.h"
#include "platform/CCFileUtils.h"

#include "base/CCConsole.h"
 // because the cocos2d-x engine has ssize_t typedef, so disable mpg123 ssize_t
#define MPG123_DEF_SSIZE_T
#include "mpg123.h"

#if CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID
#include <unistd.h>
#include <errno.h>
#endif

namespace cocos2d {

    static bool __mp3Inited = false;
    
    static ssize_t mpg123_read_r(void * handle, void * buffer, size_t count)
    {
        return ((PXFileStream*)handle)->read(buffer, count);
    }

    static off_t mpg123_lseek_r(void * handle, off_t offset, int whence)
    {
        return ((PXFileStream*)handle)->seek(offset, whence);
    }

    void mpg123_close_r(void* handle) {
        ((PXFileStream*)handle)->close();
    }
    
    bool AudioDecoderMp3::lazyInit()
    {
        bool ret = true;
        if (!__mp3Inited)
        {
            int error = mpg123_init();
            if (error == MPG123_OK)
            {
                __mp3Inited = true;
            }
            else
            {
                ALOGE("Basic setup goes wrong: %s", mpg123_plain_strerror(error));
                ret = false;
            }
        }
        return ret;
    }

    void AudioDecoderMp3::destroy()
    {
        if (__mp3Inited)
        {
            mpg123_exit();
            __mp3Inited = false;
        }
    }

    AudioDecoderMp3::AudioDecoderMp3()
    : _mpg123handle(nullptr)
    {
        lazyInit();
    }

    AudioDecoderMp3::~AudioDecoderMp3()
    {
        close();
    }

    bool AudioDecoderMp3::open(const std::string& fullPath)
    {
        long rate = 0;
        int error = MPG123_OK;
        int mp3Encoding = 0;
        int channel = 0;
        do
        {
            _mpg123handle = mpg123_new(nullptr, &error);
            if (nullptr == _mpg123handle)
            {
                ALOGE("Basic setup goes wrong: %s", mpg123_plain_strerror(error));
                break;
            }
            
            if (!_fileStream.open(fullPath))
            {
                ALOGE("Trouble with mpg123(1): %s\n", strerror(errno));
                break;
            }

            mpg123_replace_reader_handle(_mpg123handle, mpg123_read_r, mpg123_lseek_r, mpg123_close_r);

            if (mpg123_open_handle(_mpg123handle, &_fileStream) != MPG123_OK
                || mpg123_getformat(_mpg123handle, &rate, &channel, &mp3Encoding) != MPG123_OK)
            {
                ALOGE("Trouble with mpg123(2): %s\n", mpg123_strerror(_mpg123handle));
                break;
            }

            _channelCount = channel;
            _sampleRate = rate;

            if (mp3Encoding == MPG123_ENC_SIGNED_16)
            {
                _bytesPerFrame = 2 * _channelCount;
                _sourceFormat = AUDIO_SOURCE_FORMAT::PCM_16;
            }
            else if (mp3Encoding == MPG123_ENC_FLOAT_32)
            {
                _bytesPerFrame = 4 * _channelCount;
                _sourceFormat = AUDIO_SOURCE_FORMAT::PCM_FLT32;
            }
            else
            {
                ALOGE("Bad encoding: 0x%x!\n", mp3Encoding);
                break;
            }

            /* Ensure that this output format will not change (it could, when we allow it). */
            mpg123_format_none(_mpg123handle);
            mpg123_format(_mpg123handle, rate, channel, mp3Encoding);
            /* Ensure that we can get accurate length by call mpg123_length */
            mpg123_scan(_mpg123handle);

            _totalFrames = mpg123_length(_mpg123handle);

            _isOpened = true;
            return true;
        } while (false);

        if (_mpg123handle != nullptr)
        {
            mpg123_close(_mpg123handle);
            mpg123_delete(_mpg123handle);
            _mpg123handle = nullptr;
        }
        return false;
    }

    void AudioDecoderMp3::close()
    {
        if (isOpened())
        {
            if (_mpg123handle != nullptr)
            {
                mpg123_close(_mpg123handle);
                mpg123_delete(_mpg123handle);
                _mpg123handle = nullptr;
            }
            _isOpened = false;
        }
    }

    uint32_t AudioDecoderMp3::read(uint32_t framesToRead, char* pcmBuf)
    {
        int bytesToRead = framesToRead * _bytesPerFrame;
        size_t bytesRead = 0;
        int err = mpg123_read(_mpg123handle, (unsigned char*)pcmBuf, bytesToRead, &bytesRead);
        if (err == MPG123_ERR)
        {
            ALOGE("Trouble with mpg123: %s\n", mpg123_strerror(_mpg123handle) );
            return 0;
        }

        return static_cast<uint32_t>(bytesRead / _bytesPerFrame);
    }

    bool AudioDecoderMp3::seek(uint32_t frameOffset)
    {
        off_t offset = mpg123_seek(_mpg123handle, frameOffset, SEEK_SET);
        //ALOGD("mpg123_seek return: %d", (int)offset);
        if (offset >= 0 && offset == frameOffset)
        {
            return true;
        }
        return false;
    }

    uint32_t AudioDecoderMp3::tell() const
    {
        return static_cast<uint32_t>(mpg123_tell(_mpg123handle));
    }

} // namespace cocos2d {
