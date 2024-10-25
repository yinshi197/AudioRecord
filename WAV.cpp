#include <iostream>
using namespace std;

extern "C"
{
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavdevice/avdevice.h"
#include <libavutil/audio_fifo.h>
}

//WAV头部结构-PCM格式
struct WavHeader
{   
    //RIFF Chunk
    struct RIFF 
    {
        const	char ID[4] = { 'R','I', 'F', 'F' };      //ChunK ID:固定为(RIFF)
        uint32_t Size;                                   //Chunk Size:整个文件长度 - 8
        const	char Format[4] = { 'W','A', 'V', 'E' };  //Format:固定为(WAVE)
    }riff;

    //Format Chunk
    struct Format
    {
        const	char ID[4] = { 'f','m', 't', ' ' };     //Format Chunk ID:固定为(fmt )
        uint32_t Size = 16;                             //Format Chunk Size:固定为 16 (Byte)，不包含ID和Size的长度
        uint16_t AudioFormat;                           //AudioFormat:Data区块存储的音频数据的格式，PCM音频数据的值为1
        uint16_t NumChannels;                           //NumChannels:通道数量
        uint32_t SampleRate;                            //SampleRate:采样率
        uint32_t ByteRate;                              //ByteRate:比特率 = SampleRate * NumChannels * BitsPerSample / 8
        uint16_t BlockAlign;                            //BlockAlign:数据块对齐 = NumChannels * BitsPerSample/8
        uint16_t BitsPerSample;                        //BitsPerSample:采样大小
    }format;

    //Data Chunk
    struct  Data
    {
        const	char ID[4] = { 'd','a', 't', 'a' };     //Data Chunk ID:固定为(data)
        uint32_t Size;                                  //Data Chunk Size:音频数据的长度
    }data;
    WavHeader() {}
    WavHeader(int channels, int  sampleRate, int  bitsPerSample, int dataSize) 
    {
        riff.Size = 36 + dataSize;
        format.AudioFormat = 1;
        format.NumChannels = channels;
        format.SampleRate = sampleRate;
        format.ByteRate = sampleRate * channels * bitsPerSample / 8;
        format.BlockAlign = channels * bitsPerSample / 8;
        format.BitsPerSample = bitsPerSample;
        data.Size = dataSize;
    }
};

int main(int argc, char **argv)
{
    //1.初始化
    av_log_set_level(AV_LOG_DEBUG);
    avdevice_register_all();

    int ret = 0;
    char error[128];
    int count = 0;
    int dataSize = 0;

    const AVInputFormat *ifmt = nullptr;
    AVFormatContext *ifmt_ctx = nullptr;
    AVDictionary *opt = nullptr;

    AVPacket *pkt = nullptr;

    AVAudioFifo *audio_fifo = nullptr;

    const char *outfilename = "s16.pcm";
    const char *outfilename2 = "s16.wav";
    FILE *out = nullptr;
    FILE *out2 = nullptr;

    WavHeader *wavHeader = nullptr;
    
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

    out2 = fopen(outfilename2, "wb");
    if (out2 == nullptr)
    {
        cout << "FILE out is nullptr\n";
        goto end_;
    }
    
    //SEEK_SET：表示从文件开头开始seek
    fseek(out2, sizeof(WavHeader), SEEK_SET);

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
        fwrite(pkt->data, 1, pkt->size, out2);
        dataSize += pkt->size;
        cout << "pkt.size = " << pkt->size << ", count = " << count << endl;
        av_packet_unref(pkt);
        count++;     
    }

    //2.2 写入WAVHeader
    wavHeader = new WavHeader(2, 48000, 16, dataSize);
    fseek(out2, 0, SEEK_SET);
    fwrite((void*)wavHeader, 1, sizeof(WavHeader), out2); 

    fflush(out);
    fflush(out2);

    //3.释放资源
    end_:
    if (out)
    {
        fclose(out);
    }
    if (out2)
    {
        fclose(out2);
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
    if(wavHeader)
    {
        delete wavHeader;
    }

    system("pause");
    return 0;
}