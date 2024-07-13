# Linux Framebuffer Double Buffering Tests

This framebuffer test program can perform double or even triple buffering on
the framebuffer device. It's an example how to implement double/triple
buffering on the legacy Linux framebuffer device.

It is also useful for testing this legacy feature on embedded Linux devices.


## Issues/TODOs

Explain why this is the old approach. The new Linux API is DRM/KMS.

Explain the history. How I added panning and overcommit support for the
drm_kms_helper to support this feature in the framebuffer emulation for new DRM
drivers.

Write more tests

Show tricks you can do with interleaved panning.

Fix compiler warnings
