# nginx-media-module
nginx media module build for live broadcast,video on demand,video transcode,snap image,short video,file upload,file download.

module | function |  detail|   license|
-|-|-|-
nginx-rtmp-module | live on rtmp\hls\dash\flv | [nginx rtmp](https://github.com/arut/nginx-rtmp-module) | free|
nginx-vod-module | vod on hls\dash | [nginx vod](https://github.com/kaltura/nginx-vod-module) | free|
nginx-upload-module | file upload http mutil-part | [nginx upload](https://github.com/fdintino/nginx-upload-module) | free|
nginx-task-module | transcode,pull and push stream | base on libMediaKernel | [Contact me](mailto:hexinboy_sw@163.com)|
nginx-schedule-module | module schedule with zookeeper | stat info about CPU、Memory、network、disk,and sync to zk |[Contact me](mailto:hexinboy_sw@163.com)|

### 推流直播
``` mermaid
sequenceDiagram
  # 通过设定参与者(participants)的顺序控制展示模块顺序
  participant Encoder
  participant Player
　participant Control  
　participant NginxMediaModule
  participant Zookeeper

  NginxMediaModule ->>Zookeeper:1.register and report stat info.
  Control ->>Zookeeper:2.watch the module stat infoo.
  Encoder->>Control:3.get the push stream url address
  Encoder->>NginxMediaModule:4.push live stream with rtmp!
  Player->>Control: 5.get the live play url(rtmp\hls\dash)
  Player->>NginxMediaModule: 6.pull the live stream and play!
```