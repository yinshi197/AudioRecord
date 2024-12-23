# AudioRecord

## FFmpeg API采集音频

### 1.采集PCM数据

#### 相关API

- av_find_input_format("dshow")：设置采集设备格式DirectShow

- avformat_open_input()：打开输入设备，需要指定设备名字,并且可以设置采集的参数(设备需要满足才能生效)

  - 使用命令行查询采集设备列表，以dshow格式为例

    ```
    ffmpeg -list_devices true -f dshow -i dummy
    ```

    ![设备列表](./assets/image-20241023183714753.png)

    其中：**"screen-capture-recorder" (video)** 和  **"virtual-audio-capturer" (audio)** 分别是屏幕录制和系统声音采集，需要安装Screen Capturer Recorder，链接[Screen Capturer Recorder](https://github.com/rdp/screen-capture-recorder-to-video-windows-free)

  - 使用命令行查询采集设备支持的参数，以**"麦克风 (Realtek(R) Audio)"** 和  **"virtual-audio-capturer"**为例子

    ```
    ffmpeg -f dshow -list_options true -i audio="virtual-audio-capturer"
    ffmpeg -f dshow -list_options true -i audio="麦克风 (Realtek(R) Audio)"
    ```

    <img src="./assets/image-20241023184700321.png" alt="image-20241023184700321" style="zoom:80%;" />

    ch:音频通道数、bits：音频采样大小(位深)、rate：采样率，可以通过FFmpeg的AVDictionary设置

    ```c++
    av_dict_set(&opt, "channels", "2", 0);
    av_dict_set(&opt, "sample_rate", "48000", 0);
    av_dict_set(&opt, "sample_size", "16", 0);
    
    ret = avformat_open_input(&ifmt_ctx, 
                              "audio=virtual-audio-capturer",   //麦克风 (Realtek(R) Audio)
                              ifmt, 
                              &opt);
    ```

- av_read_frame()：读取PCM数据帧

使用FFmpeg采集音频数据比较简单，具体可以参考`PCM.cpp`文件。需要进行重采样操作可以参考`Resampling.cpp`文件。

### 2.播放PCM数据

可以使用**Audacity**工具播放采集的音频数据，也开始直接使用ffplay播放。

#### 方式1：Audacity工具

下载地址：[Audacity ® | Downloads](https://www.audacityteam.org/download/)

文件-->导入-->导入原始数据-->设置相关参数-->导入-->播放

![image-20241023191307214](./assets/image-20241023191307214.png)

#### 方式2：ffplay命令行播放

FFmpeg-6.0版本之后关于声道布局的参数进行了修改，6.0以下使用的是 **"-ch"** 表示声道数量，6.0以上使用 **"-ch_layout"** 表示声道布局，比如立体音stereo

```
//6.0以下
ffplay -i s16.pcm -ar 48000 -ch 2 -f s16le

//6.0以上
ffplay -i s16.pcm -ar 48000 -ch_layout stereo -f s16le
```

参数说明：

-i 表示输入文件

-ar 表示采样率

-ch_layout 表示声道布局

-f 表示采样格式

### 3.音频重采样

音频重采样就是改变原始音频的采样格式、采样率、通道布局。使用FFmpeg API进行采样率的重采样会产生变速的问题，建议使用其他开源工具进行采样率的重采样。

还有FFmpeg API的重采样为**Planer**格式不能用来播放，因为Planer是FFmpeg内部用来存储的格式，正常使用的是packet格式(交错存储)。

#### 相关API

- swr_alloc()：初始化重采样上下文

- av_opt_set_xxx()：设置原格式和目标格式的重采样参数

  ```c++
  /* 设置参数 */
  av_opt_set_chlayout(swr_ctx, "in_chlayout",    &src_ch_layout, 0);
  av_opt_set_int(swr_ctx, "in_sample_rate",       src_rate, 0);
  av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", src_sample_fmt, 0);
  
  av_opt_set_chlayout(swr_ctx, "out_chlayout",    &dst_ch_layout, 0);
  av_opt_set_int(swr_ctx, "out_sample_rate",       dst_rate, 0);
  av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_sample_fmt, 0);
  ```

- swr_init()：设置完参数后，初始化重采样上下文

- av_samples_alloc_array_and_samples()：分配样本缓冲区

  ```C++
  ret = av_samples_alloc_array_and_samples(&src_data, &src_linesize, src_nb_channels,
                                               src_nb_samples, src_sample_fmt, 0);
  ```

- av_rescale_rnd()：计算目标样本数

  ```c++
  dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, src_rate) +
                                          src_nb_samples, dst_rate, src_rate, AV_ROUND_UP);
  ```

- swr_convert()：重采样

- av_samples_get_buffer_size()：计算目标样本缓冲区大小

  ```c++
  dst_bufsize = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels,
                                                   ret, dst_sample_fmt, 1);
  
  fwrite(dst_data[0], 1, dst_bufsize, out2);  //重采样后的数据
  ```

以上就是FFmpeg重采样相关的API，具体代码参考:`Resampling.cpp`文件。

### 4.PCM To WAV

通常播放器是不能直接播放PCM数据的，因为PCM数据是纯粹的原始音频采集数据没有额外存储播放必要的信息`采样率`、`采样大小`、`通道数`

WAV格式 = WAVHead(44 Byte) + PCM，大部分播放器可以播放WAV格式的文件。

#### WAV格式分析

`WAV`文件遵循RIFF规则，其内容以区块（`chunk`）为最小单位进行存储。`WAV`文件一般由3个区块组成：`RIFF chunk`、`Format chunk`和`Data chunk`。另外，文件中还可能包含一些可选的区块，如：`Fact chunk`、`Cue points chunk`、`Playlist chunk`、`Associated data list chunk`等。

<img src="./assets/image-20241019161212778.png" alt="WAV格式" style="zoom: 80%;" />

规范文档：[Microsoft WAVE soundfile format (sapp.org)](http://soundfile.sapp.org/doc/WaveFormat/)

#### WAVHead代码实现

使用结构体封装WAVHead，只需要提供通道数、采样率、采样大小和PCM数据大小就可以分配一个44Byte的WAVHead.

> Tips：通常是采集结束后才知道PCM数据大小，可以使用fseek()跳过44字节，采集完PCM数据后再写入WAVHead

```C++
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
```

完整的代码参考`WAV.cpp`，代码比较简单仅在`PCM.cpp`基础上添加写入WAVHead功能。

<audio src="./assets/s16.wav"></audio>

## FFmpeg API AAC编码

AAC编码使用的是libfdk_aac编码器，需要重新编译FFmpeg。

具体代码参考`libfdk_aac.cpp`，如果需要重采样或者需要写ADTS Header参考`Audio.cpp`

### 1.编码过程

#### 编码器相关AIP

##### 1.avcodec_find_encoder_by_name()

通过名字查找编码器

```c++
codec = avcodec_find_encoder_by_name("libfdk_aac");
```

##### 2.avcodec_alloc_context3()

使用编码(codec)初始化编码器上下文

```C++
codec_ctx = avcodec_alloc_context3(codec);
```

##### 3.设置编码参数

在编码器上下文中设置编码的相关参数，libfdk_aac仅支持AV_SAMPLE_FMT_S16采样格式

```c++
codec_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
codec_ctx->bit_rate = 64 * 1024;
codec_ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
codec_ctx->sample_rate = 48000;
codec_ctx->profile = FF_PROFILE_AAC_HE_V2;		//三种模式 aac_low 128kb , aac_he 64kb  , aac_he_v2
codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16;
```

##### 4.avcodec_open2()

绑定编码和编码上下文,后面就通过使用编码器上下文进行编码

```c++
ret = avcodec_open2(codec_ctx, codec, nullptr);
```

#### 编码相关API

##### 1.初始化AVframe

**注意**⚠️：此时的codec_ctx->frame_size可能不等于av_read_frame()中pkt.size()，即需求单通道样本数不等于实际样本数量，直接进行后续操作是会报错的或者编码的数据播放不正常。需要使用AVAudioFifo处理这种特殊情况。

```C++
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
```

```C++
//av_read_frame()的pkt.szie是1920，采集样本数小于4096，需要使用AVAudioFifo存储数据帧。
//设置大小为frame->nb_samples(单通道样本数)
audio_fifo = av_audio_fifo_alloc(codec_ctx->sample_fmt, frame->ch_layout.nb_channels, frame->nb_samples);
```

##### 2.avcodec_send_frame()

发送原始数据帧给编码器

```C++
ret = avcodec_send_frame(codec_ctx, frame);
if (ret < 0) {
    fprintf(stderr, "Error sending the frame to the encoder\n");
    exit(1);
}

while (ret >= 0) {
    //正常，进行进行后续操作
}
```

##### 3.avcodec_receive_packet()

接收编码后的压缩数据。

可能需要几帧AVFrame数据才能编码压缩成一帧pkt数据

```c++
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

//正常，进行后续操作

av_packet_unref(pkt);
```

##### 4.最后冲刷编码器

最后可能所剩下的数据不够编码为一帧pkt数据，还需要继续发送数据。此时需要发送一个空的AVframe给编码器进行冲刷，将最后的数据编码为一帧数据。

```C++
if(frame)
{
    //释放frame后冲刷最后解码的数据，保证数据完整
    av_frame_free(&frame);
    Encodec(codec_ctx, frame, out2);
}
```

### 2.设置PTS

##### 1.**初始化PTS变量**

`base_pts` 用于记录第一个包的PTS，以便后续包的PTS可以从0开始计算。`current_pts` 用于记录当前处理的音频帧的PTS。

```c++
int64_t base_pts = 0;
int64_t current_pts = 0;
```

##### 2.**编码函数 `Encodec` 中的PTS调整**

- 在 `Encodec` 函数中，每次接收到编码后的包时，检查 `base_pts` 是否为0。如果是0，则将当前包的PTS赋值给 `base_pts`。
- 然后，将包的PTS减去 `base_pts`，使所有包的PTS从0开始。

```c++
// 调整 PTS 使其从 0 开始
if (base_pts == 0) {
    base_pts = pkt->pts;
}
pkt->pts -= base_pts;
```

##### 3.**主循环中的PTS设置**

```c++
// 设置PTS
frame->pts = current_pts;
cout << "frame->pts = " << frame->pts << endl;
Encodec(codec_ctx, frame, out2);
current_pts += frame->nb_samples;
```

- 在主循环中，每当从 `audio_fifo` 中读取到足够多的样本数据并准备编码时，设置 `frame->pts` 为 `current_pts`。
- 编码完成后，更新 `current_pts`，增加 `frame->nb_samples`，以准备下一个音频帧的PTS。

##### 4.**处理最后一帧数据**

```c++
frame->pts = current_pts;
Encodec(codec_ctx, frame, out2);
current_pts += ret;
```

- 在主循环结束后，处理 `audio_fifo` 中剩余的数据，同样设置 `frame->pts` 并调用 `Encodec` 进行编码。

### 3.使用AVAudioFifo处理特殊情况

#### 1.说明

AVAudioFifo是FFmpeg提供的一个先入先出的音频缓冲队列。主要要以下几个特点：

- 操作在样本级别而不是字节级别。
- 支持多通道的格式，不管是planar还是packed类型。
- 当写入一个已满的buffer时会自动重新分配内存。

#### 2.常用API

- av_audio_fifo_alloc()：  根据采样格式、通道数和样本个数创建一个AVAudioFifo。
- av_audio_fifo_realloc()：根据新的样本个数为AVAudioFifo重新分配空间。
- av_audio_fifo_write(): 将数据写入AVAudioFifo。如果可用的空间小于传入nb_samples参数AVAudioFifo将自动重新分配空间。
- av_audio_fifo_size(): 获取当前AVAudioFifo中可供读取的样本数量。
- av_audio_fifo_read()：从AVAudioFifo读取数据。

#### 3.处理特殊情况

##### 1.设置样本大小为Frame->nb_sample

设置样本大小为Frame->nb_sample,后续就按照该大小取每一帧数据

```c++
audio_fifo = av_audio_fifo_alloc(codec_ctx->sample_fmt, frame->ch_layout.nb_channels, frame->nb_samples);
```

##### 2.av_audio_fifo_write()

av_read_fream()读取到的每帧数据都存入audio_fifo中

```c++
av_audio_fifo_write(audio_fifo, data_ptrs, samples); //每帧数据都存入audio_fifo中
```

##### 3.av_audio_fifo_size()

判断当前audio_fifo样本数是否够取一帧数据发送给编码器

```c++
if(av_audio_fifo_size(audio_fifo) >= frame->nb_samples) {}
```

##### 4.av_audio_fifo_read()

读取一帧数据，发送给编码器

```C++
ret = av_audio_fifo_read(audio_fifo, (void**)frame->data, frame->nb_samples);
if(ret < frame->nb_samples)
{
    cout << "error,av_audio_fifo_read frame size = " << ret << endl;
    goto end_;
}
```

## libopus API OPUS编码

### Opus简单介绍

Opus 可以处理广泛的音频应用程序，包括 IP 语音、视频会议、游戏内聊天，甚至远程现场音乐表演。它可以从低比特率窄带语音扩展到非常高质量的立体声音乐。支持的功能包括：

- 比特率从 6 kb/s 到 510 kb/s
- 采样率从 8 kHz（窄带）到 48 kHz（全带）
- 帧大小从 2.5 ms 到 60 ms 不等
- 支持恒定比特率 （CBR） 和可变比特率 （VBR）
- 从窄带到全带的音频带宽
- 支持语音和音乐
- 支持单声道和立体声
- 支持多达 255 个通道（多流帧）
- 动态可调的比特率、音频带宽和帧大小
- 良好的丢包稳健性和丢包隐藏 （PLC）
- 浮点和定点实现

![image-20241024200332062](./assets/image-20241024200332062.png)

Opus官网包含：`libopus`、`opus-tools`、`opusfile`

- libopus：opus编解码的源码和可执行文件，opus_demo可以简单编码和解码opus。**这种opus文件是不能播放的，因为缺少ogg的封装**。
- opus-tools：这个工具包含了opus编码解码所需的东西，还有最重要的是(里面包含了libogg)。包含`opusenc`、`opusdec`
  - opusenc：将wav编码并转成`可播放的opus`(opusenc xxx.wav xx.opus)
  - opusdec：将`可播放的opus`转换成wav(opusdec xx.opus xx.wav)
- opusfile：是个能把`可播放的opus文件`解码成wav的工具,并且能分析出这个可播放的opus文件的信息(里面包含了libogg).

如何判断opus能不能播放？使用MediaInfo分析,可以播放的opus包含`Ogg`封装。

<center class="half">
    <img src="./assets/image-20241024202155190.png" alt="可播放的opus" style="zoom: 50%;" />
    <img src="./assets/image-20241024202346339.png" alt="不能播放的opus" style="zoom:50%;" />
</center>

### 使用libopus编码

opus 支持2.5、5、10、20、40、60ms 等帧长，对于一个48000khz 的 16bit，双通道，10 ms 的pcm音频来说，每ms 样本数为 48000/1000 = 48，采用位深为16bit/8 = 2byte，所以需要的pcm 字节数为

> pcm_size = 48 * 10 * 2 * 2 = 1920 byte

opus 编码函数是 opus_encode，其输入数组是 opus_int16 数组，2字节，要进行unsigned char 数组到 opus_int16 数组的转换后才能送入编码器。

如何验证编码后的数据是否正确呢？opus_encode()编码的opus文件是不能直接播放的。

- 方法1：手动解码编码后的数据，播放解码的文件。可以正常播放就没有问题。

- 方法2：添加附加帧长信息，使用opus_demo解码文件，播放解码后的文件。

  ```
  opus_demo -d 48000 2 .\s16.opus demo.pcm
  ```

- 方法3：封装opus为ogg格式，播放ogg文件。

#### 相关API

- opus_encoder_create():创建编码器
- opus_encoder_ctl():设置编码参数
- opus_encode():编码
- opus_encoder_destroy():销毁编码器

libopus的API比较简单,难点是输入数据的格式转换。简单的编码框架可以参考`OPUS.cpp`，包含编码和解码验证的例子参考`libopus.cpp`

##### 1.创建编码器

参数:采样率、通道数、编码模式(有三种)、结果码

```c++
encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &ret);
if(ret != OPUS_OK || encoder == nullptr)
{
    cout << "create opus encoder to failed\n";
    goto end_;
}
```

##### 2.设置编码参数

编码相关参数列表：[Opus: Encoder related CTLs](https://www.opus-codec.org/docs/opus_api-1.5/group__opus__encoderctls.html)

```c++
opus_encoder_ctl(encoder, OPUS_SET_VBR(0)); //0:CBR, 1:VBR(可变比特率 )
opus_encoder_ctl(encoder, OPUS_SET_BITRATE(64000)); //设置比特率，单位bit/s
opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(8));  //配置编码器的计算复杂度。支持的范围是 0-10（包括 0-10），其中 10 表示最高复杂性。
opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));  //配置正在编码的信号类型。给编码器提供参考
opus_encoder_ctl(encoder, OPUS_SET_LSB_DEPTH(16));  //位深(采样大小)，每个采样16个bit，2个byte。给编码器提供参考
```

##### 3.编码

```c++
ret = opus_encode(encoder, input_data, nb_samples, out_data, MAX_PACKET_SIZE);
if(ret < 0)
{
    cout << "opus_encode to failed\n";
    exit(-1);
}
```

`opus_encode` 所输出的 `data` 仅为载荷部分(纯压缩后的数据)，还需要附加帧长信息才能组成可解码的音频流。`opus_demo` 所使用的帧头结构为：

- 帧长：4 字节，大端模式
- FINAL_RANGE：4 字节，大端模式

```c++
// 大端模式下将整数转换为字节数组
void write_uint32_be(unsigned char *buf, uint32_t value)
{
    buf[0] = (value >> 24) & 0xFF;
    buf[1] = (value >> 16) & 0xFF;
    buf[2] = (value >> 8) & 0xFF;
    buf[3] = value & 0xFF;
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
```

##### 4.销毁编码器

```c++
if(encoder)
{
    opus_encoder_destroy(encoder);
}
```

以上就是libopus Opus编码的使用过程，比较简单。功能也是比较简单，只是负责将 PCM 音频数据压缩成 Opus 格式的数据包。不需要(不能)设置PTS,如果使用FFmpeg编码Opus就需要设置PTS了，因为FFmpeg使用Opus还需要重新编译，目前并没有实现该功能。后面会补上的。

## FFmpeg API Opus编码

FFmpeg可以通过引入外部库添加功能，libopus编码器就需要引入libopus库才能使用。可以自己编译，也可以使用仓库中已经编译好的FFmpeg压缩包。

> 值得注意的是，FFmpeg是内部包含Opus编码器。目前正在开发中，效果还比较差。

![image-20241027155110355](./assets/image-20241027155110355.png)

### 编码过程

编码过程和AAC编码一样，不同的是使用编码器是`libopus`,其他部分和AAC编码差不多。具体代码参考`ffmpeg_libopus.cpp`

```c++
codec = avcodec_find_encoder_by_name("libopus");
```

注意⚠️:使用libopus编码出来的opus文件也是不能直接播放的，也是纯压缩后的数据。使用FFmpeg进行封装成其他封装格式后才能播放，`ffmpeg_libopus.cpp`中使用了opus格式进行封装，封装的opus格式是FFmpeg本身自带的格式、是能`播放的opus格式`（个人理解 可以播放的opus包含`Ogg`封装,是一种封装格式）。`Ogg.cpp`中使用了Ogg进行封装，也是FFmpeg本身自带的格式。

效果：参考assets目录下的文件，Ogg封装参考`test_muxer.opus`文件,Opus封装参考`s16.ogg`文件


<center class="half">
    <audio src="./assets/s16.ogg"></audio>
    <audio src="./assets/test_muxer.opus"></audio>
</center>

### 设置参数

参数设置参考:[FFmpeg Codecs Documentation](https://ffmpeg.org/ffmpeg-codecs.html#Option-Mapping)

```c++
//设置libopus私有的参数
av_dict_set(&opt, "b", "24000", 0);    //比特率， 单位bit/s
av_dict_set(&opt, "compression_level", "8", 0); //设置编码算法复杂度, 0 ~ 10。
av_dict_set(&opt, "frame_duration", "10", 0);   //设置最大帧大小或帧的持续时间（以毫秒为单位）
av_dict_set(&opt, "application", "audio", 0);   //设置预期的应用程序类型。

ret = avcodec_open2(codec_ctx, codec, &opt);
```





## 进度

- [x] 重新编译FFmpeg
- [x] 使用FFmpeg编码Opus
- [x] 使用FFmpeg封装Ogg
- [x] PCM封装成WAV----比较简单，添加一个WAV Header就OK了。

📝：2024-10-24 22:38 完成libopus Opus编码部分。

📝：2024-10-27 15:40 完成FFmpeg Opus编码部分
