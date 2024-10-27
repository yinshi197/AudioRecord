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


int64_t base_pts = 0;
int64_t current_pts = 0;
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
        {
            av_packet_free(&pkt);
            return;
        }
            
        else if (ret < 0) {
            fprintf(stderr, "Error encoding audio frame\n");
            av_packet_free(&pkt);
            exit(1);
        }

        // 调整 PTS 使其从 0 开始
        if (base_pts == 0) {
            base_pts = pkt->pts;
        }
        pkt->pts -= base_pts;

        cout << "Encodec pkt.PTS = " << pkt->pts << endl;
        fwrite(pkt->data, 1, pkt->size, out3);
        av_packet_unref(pkt);
    }
}

int main(int argc, char **argv)
{
    //1.初始化
    av_log_set_level(AV_LOG_ERROR);
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

    const char *outfilename = "s16.pcm";
    const char *outfilename2 = "test.aac";
    FILE *out = nullptr;
    FILE *out2 = nullptr;
    
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

    pkt = av_packet_alloc();
    if (!pkt)
    {
        cout << "packet is nullptr\n";
        goto end_;
    }

    //1.2 初始化编码器
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

    //1.3初始化AVframe(存储原始数据帧----PCM数据)
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

    //av_read_frame()的pkt.szie是1920，采集样本数小于4096，需要使用AVAudioFifo存储数据帧。
    //设置大小为frame->nb_samples(单通道样本数)
    audio_fifo = av_audio_fifo_alloc(codec_ctx->sample_fmt, frame->ch_layout.nb_channels, frame->nb_samples);
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

        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);

        fwrite(pkt->data, 1, pkt->size, out);   //原始数据

        // 创建一个 void* 类型的指针数组
        void* data_ptrs[1] = { pkt->data };

        // 计算样本数
        int samples = pkt->size / (av_get_bytes_per_sample(codec_ctx->sample_fmt) * codec_ctx->ch_layout.nb_channels);
        av_audio_fifo_write(audio_fifo, data_ptrs, samples); //每帧数据都存入audio_fifo中
        cout << "audio_fifo.szie = " << av_audio_fifo_size(audio_fifo) << endl;

        //audio_fifo中的数据满足一个AVframe就取出一帧数据送入解码
        if(av_audio_fifo_size(audio_fifo) >= frame->nb_samples)
        {
            ret = av_audio_fifo_read(audio_fifo, (void**)frame->data, frame->nb_samples);
            if(ret < frame->nb_samples)
            {
                cout << "error,av_audio_fifo_read frame size = " << ret << endl;
                goto end_;
            }

            // 设置PTS
            frame->pts = current_pts;
            cout << "frame->pts = " << frame->pts << endl;
            Encodec(codec_ctx, frame, out2);
            current_pts += frame->nb_samples;
        }

        av_packet_unref(pkt);
        count++;     
    }

    //最后一帧数据全部送入编码器。
    ret = av_audio_fifo_read(audio_fifo, (void**)frame->data, av_audio_fifo_size(audio_fifo));
    if(ret > 0) 
    {
        frame->pts = current_pts;
        Encodec(codec_ctx, frame, out2);
        current_pts += ret;
    }

    fflush(out);

    //3.释放资源
    end_:
    if (out)
    {
        fclose(out);
    }
    if(frame)
    {
        //释放frame后冲刷最后解码的数据，保证数据完整
        av_frame_free(&frame);
        Encodec(codec_ctx, frame, out2);
        fflush(out2);
    }
    if(out2)
    {
        fclose(out2);
    }
    if(pkt)
    {
        av_packet_free(&pkt);
    }
    if(opt)
    {
        av_dict_free(&opt);
    }
    if(ifmt_ctx)
    {
        avformat_close_input(&ifmt_ctx);
    }
    if(codec_ctx)
    {
        avcodec_free_context(&codec_ctx);
    }

    system("pause");
    return 0;
}