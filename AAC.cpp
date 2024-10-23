#include <iostream>
#include <fstream>
using namespace std;

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/opt.h>
#include <libavdevice/avdevice.h>
}

/* 检测该编码器是否支持该采样格式 */
static int check_sample_fmt(const AVCodec* codec, enum AVSampleFormat sample_fmt)
{
	const enum AVSampleFormat* p = codec->sample_fmts;

	while (*p != AV_SAMPLE_FMT_NONE) { // 通过AV_SAMPLE_FMT_NONE作为结束符
		if (*p == sample_fmt)
			return 1;
		p++;
	}
	return 0;
}

/* 检测该编码器是否支持该采样率 */
static int check_sample_rate(const AVCodec* codec, const int sample_rate)
{
	const int* p = codec->supported_samplerates;
	while (*p != 0) {// 0作为退出条件，比如libfdk-aacenc.c的aac_sample_rates
		cout << codec->name << " support " << *p << "hz" << endl;
		if (*p == sample_rate)
			return 1;
		p++;
	}
	return 0;
}

/* 检测该编码器是否支持该通道布局, 该函数只是作参考 */
static int check_channel_layout(const AVCodec* codec, AVChannelLayout channel_layout)
{
	// 不是每个codec都给出支持的channel_layout
	const AVChannelLayout* p = &channel_layout;
	
	if (!p) 
	{
		cout << "the codec" << codec->name << " no set channel_layouts" << endl;
		return 1;
	}
	if (av_channel_layout_check(p))  //如果 channel_layout 有效，则返回 1，否则返回 0
		return 1;
	else return 0;
}

static void get_adts_header(AVCodecContext* ctx, uint8_t* adts_header, int aac_length)
{
	uint8_t freq_idx = 0;    //0: 96000 Hz  3: 48000 Hz 4: 44100 Hz
	switch (ctx->sample_rate) {
	case 96000: freq_idx = 0; break;
	case 88200: freq_idx = 1; break;
	case 64000: freq_idx = 2; break;
	case 48000: freq_idx = 3; break;
	case 44100: freq_idx = 4; break;
	case 32000: freq_idx = 5; break;
	case 24000: freq_idx = 6; break;
	case 22050: freq_idx = 7; break;
	case 16000: freq_idx = 8; break;
	case 12000: freq_idx = 9; break;
	case 11025: freq_idx = 10; break;
	case 8000: freq_idx = 11; break;
	case 7350: freq_idx = 12; break;
	default: freq_idx = 4; break;
	}
	uint8_t chanCfg = ctx->ch_layout.nb_channels;
	uint32_t frame_length = aac_length + 7;
	adts_header[0] = 0xFF;
	adts_header[1] = 0xF1;
	adts_header[2] = ((ctx->profile) << 6) + (freq_idx << 2) + (chanCfg >> 2);
	adts_header[3] = (((chanCfg & 3) << 6) + (frame_length >> 11));
	adts_header[4] = ((frame_length & 0x7FF) >> 3);
	adts_header[5] = (((frame_length & 7) << 5) + 0x1F);
	adts_header[6] = 0xFC;
}

/*
*
*/
static int encode(AVCodecContext* ctx, AVFrame* frame, AVPacket* pkt, FILE* output)
{
	int ret;

	/* send the frame for encoding */
	ret = avcodec_send_frame(ctx, frame);
	if (ret < 0) {
		fprintf(stderr, "Error sending the frame to the encoder\n");
		return -1;
	}

	/* read all the available output packets (in general there may be any number of them */
	// 编码和解码都是一样的，都是send 1次，然后receive多次, 直到AVERROR(EAGAIN)或者AVERROR_EOF
	while (ret >= 0) {
		ret = avcodec_receive_packet(ctx, pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			return 0;
		}
		else if (ret < 0) {
			cout << "Error encoding audio frame\n";
			return -1;
		}

		size_t len = 0;
		printf("ctx->flags:0x%x & AV_CODEC_FLAG_GLOBAL_HEADER:0x%x, name:%s\n", ctx->flags, ctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER, ctx->codec->name);
		if ((ctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER)) {
			// 需要额外的adts header写入
			uint8_t aac_header[7];
			get_adts_header(ctx, aac_header, pkt->size);
			len = fwrite(aac_header, 1, 7, output);
			if (len != 7) {
				fprintf(stderr, "fwrite aac_header failed\n");
				return -1;
			}
		}
		len = fwrite(pkt->data, 1, pkt->size, output);
		if (len != pkt->size) {
			fprintf(stderr, "fwrite aac data failed\n");
			return -1;
		}
		/* 是否需要释放数据? avcodec_receive_packet第一个调用的就是 av_packet_unref
		* 所以我们不用手动去释放，这里有个问题，不能将pkt直接插入到队列，因为编码器会释放数据
		* 可以新分配一个pkt, 然后使用av_packet_move_ref转移pkt对应的buffer
		*/
		// av_packet_unref(pkt);
	}
	return -1;
}

/*
 * 这里只支持2通道的转换
*/
void f32le_convert_to_fltp(float* f32le, float* fltp, int nb_samples) 
{
	float* fltp_l = fltp;   // 左通道
	float* fltp_r = fltp + nb_samples;   // 右通道
	for (int i = 0; i < nb_samples; i++) {
		fltp_l[i] = f32le[i * 2];     // 0 1   - 2 3
		fltp_r[i] = f32le[i * 2 + 1];   // 可以尝试注释左声道或者右声道听听声音
	}
}
/*
 * 提取测试文件：
 * （1）s16格式：ffmpeg -i buweishui.aac -ar 48000 -ac 2 -f s16le 48000_2_s16le.pcm
 * （2）flt格式：ffmpeg -i buweishui.aac -ar 48000 -ac 2 -f f32le 48000_2_f32le.pcm
 *      ffmpeg只能提取packed格式的PCM数据，在编码时候如果输入要为fltp则需要进行转换
 * 测试范例:
 * （1）48000_2_s16le.pcm libfdk_aac.aac libfdk_aac  // 如果编译的时候没有支持fdk aac则提示找不到编码器
 * （2）48000_2_f32le.pcm aac.aac aac // 我们这里只测试aac编码器，不测试fdkaac
*/

int main(int argc, char* argv[])
{
	const char* ifilename = NULL;
	const char* ofilename = NULL;
	FILE* in_file;
	FILE* out_file;
	const AVCodec* codec = nullptr;  //编码器
	AVCodecContext* codec_ctx = nullptr; //编码器上下文
	AVFrame* frame = nullptr;
	AVPacket* pkt = nullptr;
	int ret = 0;
	int force_code = 0;
	const char* codec_name = NULL;

	if (argc < 3)
	{
		cout << "Usage: " << argv[0] << "<input_file out_file[codec_name]>, argc:" << argc << endl;
		return 0;
	}

	ifilename = argv[1];
	ofilename = argv[2];

	enum AVCodecID  codec_id = AV_CODEC_ID_AAC;

	if (argc == 4)
	{
		if (strcmp(argv[3], "libfdk_aac") == 0)
		{
			force_code = 1; //强制使用libfdk_aac
			codec_name = "libfdk_aac";
		}
		else if (strcmp(argv[3], "aac") == 0)
		{
			force_code = 1; //强制使用aac
			codec_name = "aac";
		}
	}
	if (force_code)
	{
		cout << "force codec name:" << codec_name << endl;
		codec = avcodec_find_encoder_by_name(codec_name);
	}
	else
	{
		cout << "default codec name:" << codec_name << endl;
		codec = avcodec_find_encoder(codec_id);
	}
	if (!codec)
	{
		cout << "codec is nullptr,find codec is fail" << endl;
		exit(-1);
	}

	codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx)
	{
		cout << "codec_ctx is nullptr,alloc codec_ctx is fail" << endl;
		exit(-1);
	}

	AVChannelLayout ch_layout2 = AV_CHANNEL_LAYOUT_STEREO;
	codec_ctx->ch_layout = ch_layout2;

	codec_ctx->codec_id = codec_id;
	codec_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
	codec_ctx->bit_rate = 128 * 1024;
	codec_ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
	codec_ctx->sample_rate = 44100;   //48000
	//codec_ctx->ch_layout.nb_channels = codec_ctx->ch_layout.nb_channels;
	codec_ctx->profile = FF_PROFILE_AAC_LOW;
	if (strcmp(codec->name, "aac") == 0) {
		codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
	}
	else if (strcmp(codec->name, "libfdk_aac") == 0) {
		codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16;
	}
	else {
		codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
	}

	//检查采样格式是否支持 return 0不支持，1支持
	if (!check_sample_fmt(codec, codec_ctx->sample_fmt))
	{
		cout << "Encoder does not support sample format " << av_get_sample_fmt_name(codec_ctx->sample_fmt) << endl;
		exit(-1);
	}

	if (!check_sample_rate(codec, codec_ctx->sample_rate))
	{
		cout << "Encoder does not support sample rate " << codec_ctx->sample_rate << endl;
		exit(-1);
	}

	if (!check_channel_layout(codec, codec_ctx->ch_layout))
	{
		char buff[64] = { 0 };
		av_channel_layout_describe(&codec_ctx->ch_layout, buff, 64);
		cout << "Encoder does not support channel layout " << buff << endl;
		exit(-1);
	}

	cout << "\n\nAudio Encodec config" << endl;
	cout << "bit_rate: " << codec_ctx->bit_rate/1024 << "kbps" << endl;
	cout << "sample_rate: " << codec_ctx->sample_rate << endl;
	cout << "sample_fmt: " << av_get_sample_fmt_name(codec_ctx->sample_fmt) << endl;
	cout << "channles: " << codec_ctx->ch_layout.nb_channels << endl;
	cout << "1 frame size: " << codec_ctx->frame_size << endl;
	codec_ctx->flags = AV_CODEC_FLAG_GLOBAL_HEADER;  //ffmpeg默认的aac是不带adts，而fdk_aac默认带adts，这里我们强制不带

	//将编码器和编码器上下文进行关联
	if (avcodec_open2(codec_ctx, codec, NULL) < 0)
	{
		cout << "avcodec_open2 fail" << endl;
	}
	cout << "2 frame size " << codec_ctx->frame_size << endl; // 决定每次到底送多少个采样点

	in_file = fopen(ifilename,"rb");
	if (!in_file)
	{
		cout << "open file " << ifilename << "fail" << endl;
		exit(-1);
	}
	out_file = fopen(ofilename, "wb");
	if (!out_file)
	{
		cout << "open file " << ofilename << "fail" << endl;
		exit(-1);
	}

	pkt = av_packet_alloc();
	if (!pkt)
	{
		cout << "pkt alloc fail" << endl;
		exit(-1);
	}
	frame = av_frame_alloc();
	if (!frame)
	{
		cout << "frame alloc fail" << endl;
		exit(-1);
	}

	/* 每次送多少数据给编码器由：
	 *  (1)frame_size(每帧单个通道的采样点数);
	 *  (2)sample_fmt(采样点格式);
	 *  (3)channel_layout(通道布局情况);
	 * 3要素决定
	 */
	frame->nb_samples = codec_ctx->frame_size;
	frame->format = codec_ctx->sample_fmt;
	frame->ch_layout = codec_ctx->ch_layout;
	cout << "frame nb_samples:" << frame->nb_samples << endl;
	cout << "frame sample_fmt: " << frame->format << endl;
	char buff[64] = { 0 };
	av_channel_layout_describe(&frame->ch_layout, buff, 64);  //描述通道布局
	cout << "frame channel_layout: " << buff << endl;
	
	/*为frame分配buffer*/
	ret = av_frame_get_buffer(frame,0);
	if (ret < 0)
	{
		cout << "Could not allocate frame buff" << endl;
	}

	// 计算出每一帧的数据 单个采样点的字节 * 通道数目 * 每帧采样点数量
	int frame_bytes = av_get_bytes_per_sample((AVSampleFormat)frame->format) \
		* frame->ch_layout.nb_channels \
		* frame->nb_samples;
	cout << "frame_bytes: " << frame_bytes << endl;
	uint8_t* pcm_buf = (uint8_t*)malloc(frame_bytes);
	if (!pcm_buf) {
		cout << "pcm_buf malloc failed\n";
		return 1;
	}
	uint8_t* pcm_temp_buf = (uint8_t*)malloc(frame_bytes);
	if (!pcm_temp_buf) {
		cout << "pcm_temp_buf malloc failed\n";
		return 1;
	}
	int64_t pts = 0;
	cout << "start enode\n";
	for (;;)
	{
		memset(pcm_buf, 0, frame_bytes);	//填充pcm_buf内存为全0
		size_t read_bytes = fread(pcm_buf, 1, frame_bytes, in_file);
		if (read_bytes <= 0) 
		{
			cout << "read file finish\n";
			break;
			//            fseek(infile,0,SEEK_SET);
			//            fflush(outfile);
			//            continue;
		}

		/* 确保该frame可写, 如果编码器内部保持了内存参考计数，则需要重新拷贝一个备份
		   目的是新写入的数据和编码器保存的数据不能产生冲突
	   */
		ret = av_frame_make_writable(frame);
		if (ret != 0)
			cout << "av_frame_make_writable failed, ret = " << ret << endl;

		if (AV_SAMPLE_FMT_S16 == frame->format) {
			// 将读取到的PCM数据填充到frame去，但要注意格式的匹配, 是planar还是packed都要区分清楚
			ret = av_samples_fill_arrays(frame->data, frame->linesize,
				pcm_buf, frame->ch_layout.nb_channels,
				frame->nb_samples, (AVSampleFormat)frame->format, 0);
		}
		else {
			// 将读取到的PCM数据填充到frame去，但要注意格式的匹配, 是planar还是packed都要区分清楚
			// 将本地的f32le packed模式的数据转为float palanar
			memset(pcm_temp_buf, 0, frame_bytes);
			f32le_convert_to_fltp((float*)pcm_buf, (float*)pcm_temp_buf, frame->nb_samples);
			ret = av_samples_fill_arrays(frame->data, frame->linesize,
				pcm_temp_buf, frame->ch_layout.nb_channels,
				frame->nb_samples, (AVSampleFormat)frame->format, 0);
		}

		// 设置pts
		pts += frame->nb_samples;
		frame->pts = pts;       // 使用采样率作为pts的单位，具体换算成秒 pts*1/采样率
		ret = encode(codec_ctx, frame, pkt, out_file);
		if (ret < 0) {
			cout << "encode failed\n";
			break;
		}
	}

	/* 冲刷编码器 */
	encode(codec_ctx, NULL, pkt, out_file);

	// 关闭文件
	fclose(in_file);
	fclose(out_file);

	// 释放内存
	if (pcm_buf) {
		free(pcm_buf);
	}
	if (pcm_temp_buf) {
		free(pcm_temp_buf);
	}
	av_frame_free(&frame);
	av_packet_free(&pkt);
	avcodec_free_context(&codec_ctx);
	cout << "main finish, please enter Enter and exit\n";
	getchar();


	return 0;
}