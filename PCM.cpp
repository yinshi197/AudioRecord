#include <iostream>
using namespace std;

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavdevice/avdevice.h"
}

int main(int argc, char **argv)
{
    //1.初始化
    av_log_set_level(AV_LOG_DEBUG);
    avdevice_register_all();

    int ret = 0;
    char error[128];
    int count = 0;

    const AVInputFormat *ifmt = nullptr;
    AVFormatContext *ifmt_ctx = nullptr;
    AVDictionary *opt = nullptr;

    AVPacket *pkt = nullptr;

    const char *outfilename = "s16.pcm";
    FILE *out = nullptr;
    
    ifmt = av_find_input_format("dshow");
    
    av_dict_set(&opt, "channels", "2", 0);
    av_dict_set(&opt, "sample_rate", "48000", 0);
    av_dict_set(&opt, "sample_size", "16", 0);

    ret = avformat_open_input(&ifmt_ctx, 
                              "audio=virtual-audio-capturer",   //麦克风 (Realtek(R) Audio)
                              ifmt, 
                              &opt);
    if (ret < 0)
    {
        av_strerror(ret, error, sizeof(error) - 1);
        av_log(nullptr, AV_LOG_INFO, "avformat_open_input is error, %s\n", error);

        goto end_;
    }

    out = fopen(outfilename, "wb");
    if (out == nullptr)
    {
        cout << "FILE out is nullptr\n";
        goto end_;
    }

    pkt = av_packet_alloc();
    if (!pkt)
    {
        cout << "packet is nullptr\n";
        goto end_;
    }

    //2.采集音频数据
    while (av_read_frame(ifmt_ctx, pkt) == 0 && count < 2000)
    {
        fwrite(pkt->data, 1, pkt->size, out);   //原始数据
        cout << "pkt.size = " << pkt->size << ", count = " << count << endl;
        av_packet_unref(pkt);
        count++;     
    }

    fflush(out);

    //3.释放资源

    end_:
    if (out)
    {
        fclose(out);
    }
    if (pkt)
    {
        av_packet_free(&pkt);
    }
    if (ifmt_ctx)
    {
        avformat_close_input(&ifmt_ctx);
    }
    if (opt)
    {
        av_dict_free(&opt);
    }

    system("pause");
    return 0;
}