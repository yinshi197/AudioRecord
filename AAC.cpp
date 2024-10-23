#include <iostream>
using namespace std;

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavdevice/avdevice.h"
#include "libavutil/avutil.h"
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/audio_fifo.h>
}

//不支持fltp、不支持fltp、不支持fltp
static int get_format_from_sample_fmt(const char **fmt,
                                      enum AVSampleFormat sample_fmt)
{
    int i;
    struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt; const char *fmt_be, *fmt_le;
    } sample_fmt_entries[] = {
        { AV_SAMPLE_FMT_U8,  "u8",    "u8"    },
        { AV_SAMPLE_FMT_S16, "s16be", "s16le" },
        { AV_SAMPLE_FMT_S32, "s32be", "s32le" },
        { AV_SAMPLE_FMT_FLT, "f32be", "f32le" },
        { AV_SAMPLE_FMT_DBL, "f64be", "f64le" },
    };
    *fmt = NULL;
 
    for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt) {
            *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
            return 0;
        }
    }
 
    fprintf(stderr,
            "Sample format %s not supported as output format\n",
            av_get_sample_fmt_name(sample_fmt));
    return AVERROR(EINVAL);
}
 
void Encodec(AVCodecContext *codec_ctx, AVFrame *frame, FILE *out3)
{
    int ret;
    AVPacket *pkt = av_packet_alloc();
    ret = avcodec_send_frame(codec_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending the frame to the encoder\n");
        exit(1);
    }
 
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error encoding audio frame\n");
            exit(1);
        }
 
        fwrite(pkt->data, 1, pkt->size, out3);
        av_packet_unref(pkt);
    }
}

void f32le_convert_to_fltp(float* f32le, float* fltp, int nb_samples)
{
    float* fltp_l = fltp;	//左通道
	float* fltp_r = fltp + nb_samples;	//右通道
	for (int i = 0; i < nb_samples; i++)
	{
		fltp_l[i] = f32le[i * 2];	// 0(左) 1(右)  ----  2 3
		fltp_r[i] = f32le[i * 2 + 1]; // 可以尝试注释单个声道
	}
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
    AVCodecContext *codec_ctx = nullptr;
    const AVCodec *codec = nullptr;
    AVDictionary *opt = nullptr;

    AVPacket *pkt = nullptr;
    AVFrame *frame = nullptr;

    AVAudioFifo *audio_fifo = nullptr;

    //重采样参数
    AVChannelLayout src_ch_layout = AV_CHANNEL_LAYOUT_STEREO, dst_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    int src_rate = 48000, dst_rate = 48000;
    uint8_t **src_data = NULL, **dst_data = NULL;
    int src_nb_channels = 0, dst_nb_channels = 0;
    int src_linesize, dst_linesize;
    int src_nb_samples = 1920 / 4, dst_nb_samples, max_dst_nb_samples;
    enum AVSampleFormat src_sample_fmt = AV_SAMPLE_FMT_S16, dst_sample_fmt = AV_SAMPLE_FMT_FLT;
    int dst_bufsize;
    const char *fmt;
    struct SwrContext *swr_ctx;
    char buf[64];

    const char *outfilename = "s16.pcm";
    const char *outfilename2 = "flt.pcm";
    const char *outfilename3 = "test.aac";
    FILE *out = nullptr;
    FILE *out2 = nullptr;
    FILE *out3 = nullptr;

    if ((ret = get_format_from_sample_fmt(&fmt, dst_sample_fmt)) < 0)
        goto end_;
    
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
    if (out3 == nullptr)
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

    /* 重采样上下文 */
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        fprintf(stderr, "Could not allocate resampler context\n");
        ret = AVERROR(ENOMEM);
        goto end_;
    }
 
    /* 设置参数 */
    av_opt_set_chlayout(swr_ctx, "in_chlayout",    &src_ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate",       src_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", src_sample_fmt, 0);
 
    av_opt_set_chlayout(swr_ctx, "out_chlayout",    &dst_ch_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate",       dst_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_sample_fmt, 0);
 
    /* 初始化重采样上下文 */
    if ((ret = swr_init(swr_ctx)) < 0) 
    {
        fprintf(stderr, "Failed to initialize the resampling context\n");
        goto end_;
    }
 
    /* 分配源和目标样本缓冲区 */
 
    src_nb_channels = src_ch_layout.nb_channels;
    ret = av_samples_alloc_array_and_samples(&src_data, &src_linesize, src_nb_channels,
                                             src_nb_samples, src_sample_fmt, 0);
    if (ret < 0) 
    {
        fprintf(stderr, "Could not allocate source samples\n");
        goto end_;
    }

    //避免溢出,向上取整计算最大dst_nb_samples
    max_dst_nb_samples = dst_nb_samples =
        av_rescale_rnd(src_nb_samples, dst_rate, src_rate, AV_ROUND_UP);

    /* 缓冲区将直接写入原始音频文件，无需对齐 */
    dst_nb_channels = dst_ch_layout.nb_channels;
    ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, dst_nb_channels,
                                             dst_nb_samples, dst_sample_fmt, 0);

    if (ret < 0) 
    {
        fprintf(stderr, "Could not allocate destination samples\n");
        goto end_;
    }

    //1.3 初始化编码器
    codec = avcodec_find_encoder_by_name("libfdk_aac");
    if(!codec)
    {
        cout << "avcodec_find_encoder_by_name to failed\n";
        goto end_;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if(!codec_ctx)
    {
        cout << "avcodec_alloc_context3 to failed\n";
        goto end_;
    }

    codec_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
	codec_ctx->bit_rate = 64 * 1024;
	codec_ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
	codec_ctx->sample_rate = 48000;
	codec_ctx->profile = FF_PROFILE_AAC_HE_V2;		//三种模式 aac_low 128kb , aac_he 64kb  , aac_he_v2
    codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16;

    ret = avcodec_open2(codec_ctx, codec, nullptr);
    if(ret < 0)
    {
        av_strerror(ret, error, sizeof(error) - 1);
        av_log(nullptr, AV_LOG_INFO, "avcodec_open2 is error, %s\n", error);

        goto end_;
    }

    frame = av_frame_alloc();
    /* 每次送多少数据给编码器由：
	*  (1)frame_size(每帧单个通道的采样点数);
	*  (2)sample_fmt(采样点格式);
	*  (3)channel_layout(通道布局情况);
	*  3个要素决定
	*/

	frame->nb_samples = codec_ctx->frame_size;
	frame->format = codec_ctx->sample_fmt;
	frame->ch_layout = codec_ctx->ch_layout;

    ret = av_frame_get_buffer(frame, 0);
	if (ret < 0)
	{
		cout << "av_frame_get_buffer failed" << endl;
        goto end_;
	}

    audio_fifo = av_audio_fifo_alloc(src_sample_fmt, src_ch_layout.nb_channels, frame->nb_samples);
    if(!audio_fifo)
    {
        cout << "audio_fifo is nullptr\n";
        goto end_;
    }

    //2.采集音频数据
    //采样间隔默认是10ms，最终时长为20s
    while (av_read_frame(ifmt_ctx, pkt) == 0 && count < 2000)
    {
        cout << "pkt.size = " << pkt->size << endl;
        /* 计算目标样本数 */
        dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, src_rate) +
                                        src_nb_samples, dst_rate, src_rate, AV_ROUND_UP);
        if (dst_nb_samples > max_dst_nb_samples) {
            av_freep(&dst_data[0]);
            ret = av_samples_alloc(dst_data, &dst_linesize, dst_nb_channels,
                                   dst_nb_samples, dst_sample_fmt, 1);
            if (ret < 0)
                break;
            max_dst_nb_samples = dst_nb_samples;
        }
        memcpy(src_data[0], (void*)pkt->data, pkt->size);
        /* 开始转换 */
        ret = swr_convert(swr_ctx, dst_data, dst_nb_samples, (const uint8_t **)src_data, src_nb_samples);
        if (ret < 0) {
            fprintf(stderr, "Error while converting\n");
            goto end_;
        }
        dst_bufsize = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels,
                                                 ret, dst_sample_fmt, 1);
        if (dst_bufsize < 0) {
            fprintf(stderr, "Could not get sample buffer size\n");
            goto end_;
        }
        printf("count:%d in:%d out:%d\n", count, src_nb_samples, ret);
        
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);

        fwrite(pkt->data, 1, pkt->size, out);   //原始数据
        fwrite(dst_data[0], 1, dst_bufsize, out2);  //重采样后的数据
        av_audio_fifo_write(audio_fifo, (void **)src_data, src_nb_samples);
        cout << "audio_fifo.szie = " << av_audio_fifo_size(audio_fifo) << endl;
        if(av_audio_fifo_size(audio_fifo) >= frame->nb_samples)
        {
            ret = av_audio_fifo_read(audio_fifo, (void**)frame->data, frame->nb_samples);
            if(ret < frame->nb_samples)
            {
                cout << "error,av_audio_fifo_read frame size = " << ret << endl;
                goto end_;
            }
            Encodec(codec_ctx, frame, out3);
        }

        av_packet_unref(pkt);
        count++;     
    }

    //最后一帧数据全部送入编码器。
    ret = av_audio_fifo_read(audio_fifo, (void**)frame->data, av_audio_fifo_size(audio_fifo));
    Encodec(codec_ctx, frame, out3);

    fflush(out);
    fflush(out2);

    //4.释放资源

    end_:
    if (out)
    {
        fclose(out);
    }
    if (out2)
    {
        fclose(out2);
    }
    if(frame)
    {
        //释放frame后冲刷最后解码的数据，保证数据完整
        av_frame_free(&frame);
        Encodec(codec_ctx, frame, out3);
    }
    if(out3)
    {
        fclose(out3);
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
    if(swr_ctx)
    {
        swr_free(&swr_ctx);
    }
    if(codec_ctx)
    {
        avcodec_free_context(&codec_ctx);
    }

    system("pause");
    return 0;
}