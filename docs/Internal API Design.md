# Internal API Design

## Interface

IMediaSource

```cpp
class IMediaSource
{
public:

    virtual bool open() = 0;

    virtual void close() = 0;

    virtual Frame* getFrame() = 0;

};
```

Implement

- FileSource
- RTSPSource
- NDISource
- ImageSource

Media Engine chỉ làm việc với IMediaSource.

Không cần biết loại Source.

Điều này giúp mở rộng dễ dàng.