#include <iostream>
#include "opus.h"
using namespace std;

extern "C"
{
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavdevice/avdevice.h"
}

#define MAX_PACKET_SIZE     4000

// 大端模式下将整数转换为字节数组
void write_uint32_be(unsigned char *buf, uint32_t value)
{
    buf[0] = (value >> 24) & 0xFF;
    buf[1] = (value >> 16) & 0xFF;
    buf[2] = (value >> 8) & 0xFF;
    buf[3] = value & 0xFF;
}

void Encoder(OpusEncoder *encoder, AVPacket *pkt, FILE *out, OpusDecoder *decoder, FILE *out2)
{   
    opus_int32 ret = 0;

    // 计算输入数据的样本数
    int nb_samples = pkt->size / sizeof(opus_int16) / 2;

    // 创建输入数据缓冲区
    opus_int16 input_data[nb_samples];

    // 将 AVPacket 中的数据复制到 input_data
    memcpy(input_data, pkt->data, pkt->size);

    // 分配输出数据缓冲区
    unsigned char out_data[MAX_PACKET_SIZE];
    ret = opus_encode(encoder, input_data, nb_samples, out_data, MAX_PACKET_SIZE);
    if(ret < 0)
    {
        cout << "opus_encode to failed\n";
        exit(-1);
    }

    // 创建帧头
    unsigned char frame_header[8];
    write_uint32_be(frame_header, ret); // 帧长
    // FINAL_RANGE
    uint32_t enc_final_range;
    opus_encoder_ctl(encoder, OPUS_GET_FINAL_RANGE(&enc_final_range));
    write_uint32_be(frame_header + 4, enc_final_range); // FINAL_RANGE

    // (使用opus_demo解码)写入帧头和编码后的数据,使用 "opus_demo -d 48000 2 s16.opus demo.pcm" 解码可以正常播放demo.pcm就没问题。
    fwrite(frame_header, 1, 8, out);
    fwrite(out_data, 1, ret, out);

    // (手动解码)解码和原来数据比对，可以播放就表示编码没问题，即decoder.pcm可以正常播放.
    opus_int16 out_decoder[nb_samples * 2]; //双通道交错存储，length is frame_size*channels*sizeof(opus_int16)
    int out_size = 48 * 10;
    auto frame_size = opus_decode(decoder, out_data, ret, out_decoder, out_size, 0);
    
    cout << "ret = " << ret << ", frame_size = " << frame_size << endl;
    if (frame_size < 0)
    {
       printf("解码失败\n");
       return;
    }

    fwrite(out_decoder, 1, frame_size * 4, out2);
    
    //如果目标平台的字节序不一致，就需要使用下面的方法，手动拆分每个 16 位值
    //unsigned char pcm_bytes[MAX_PACKET_SIZE];
    //为什么是frame_size * 2
    //frame_size 是解码后的样本数，对于双通道音频，每个样本实际上包含两个 16 位的值。
    // for (auto i = 0; i < frame_size * 2; i++)
    // {
    //     pcm_bytes[2 * i] = out_decoder[i] & 0xFF;
    //     pcm_bytes[2 * i + 1] = (out_decoder[i] >> 8) & 0xFF;
    // }
    //fwrite(pcm_bytes, 1, frame_size * 4, out2);
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

    OpusEncoder *encoder = nullptr;
    OpusDecoder *decoder = nullptr;

    AVPacket *pkt = nullptr;

    const char *outfilename = "s16.pcm";
    const char *outfilename2 = "s16.opus";
    const char *outfilename3 = "decoder.pcm";
    FILE *out = nullptr;
    FILE *out2 = nullptr;
    FILE *out3 = nullptr;
    
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

    out3 = fopen(outfilename3, "wb");
    if (out2 == nullptr)
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

    //1.2 初始化opus编码器
    encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &ret);
    if(ret != OPUS_OK || encoder == nullptr)
    {
        cout << "create opus encoder to failed\n";
        goto end_;
    }

    opus_encoder_ctl(encoder, OPUS_SET_VBR(0)); //0:CBR, 1:VBR(可变比特率 )
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(64000)); //设置比特率，单位bit/s
    opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(8));  //配置编码器的计算复杂度。支持的范围是 0-10（包括 0-10），其中 10 表示最高复杂性。
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));  //配置正在编码的信号类型。给编码器提供参考
    opus_encoder_ctl(encoder, OPUS_SET_LSB_DEPTH(16));  //位深(采样大小)，每个采样16个bit，2个byte。给编码器提供参考

    decoder = opus_decoder_create(48000, 2, &ret);
    if(ret < 0 || encoder == nullptr)
    {
        cout << "create opus decoder to failed\n";
        goto end_;
    }
    opus_decoder_ctl(decoder, OPUS_SET_LSB_DEPTH(16));

    //2.采集音频数据
    while (av_read_frame(ifmt_ctx, pkt) == 0 && count < 2000)
    {
        fwrite(pkt->data, 1, pkt->size, out);   //原始数据
        cout << "pkt.size = " << pkt->size << ", count = " << count << endl;
        Encoder(encoder, pkt, out2, decoder, out3);
        av_packet_unref(pkt);
        count++;     
    }

    fflush(out);
    fflush(out2);
    fflush(out3);

    //3.释放资源
    end_:
    if(out)
    {
        fclose(out);
    }
    if(out2)
    {
        fclose(out2);
    }
    if(out3)
    {
        fclose(out3);
    }
    if(pkt)
    {
        av_packet_free(&pkt);
    }
    if(ifmt_ctx)
    {
        avformat_close_input(&ifmt_ctx);
    }
    if(opt)
    {
        av_dict_free(&opt);
    }
    if(encoder)
    {
        opus_encoder_destroy(encoder);
    }
    if(decoder)
    {
        opus_decoder_destroy(decoder);
    }

    system("pause");
    return 0;
}