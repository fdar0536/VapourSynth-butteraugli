# VapourSynth-butteraugli

## Introduction

It's base on [Google's butteraugli](https://github.com/google/butteraugli).  
Please refer to [the original document](https://github.com/google/butteraugli/blob/master/README.md).

## Usage

```python
Butteraugli.butteraugli(clip clipa, clip clipb)
```

- Both clipa and clipb MUST be in RGB24 format.
- It returns a clip contains differences between two input clips, and stores diffvalue in the frame properties named "_Diff".

## Example

```python
import mvsfunc as mvf

clipa = core.std.Trim(src1, 0, 0)
clipa = mvf.ToRGB(clipa, depth=8)
clipb = core.std.Trim(src2, 0, 0)
clipb = mvf.ToRGB(clipb, depth=8)

diff = core.Butteraugli.butteraugli(clipa, clipb)
```

## Compilation

```shell
meson build
ninja -C build
```

## Licence

[Apache 2.0](http://www.apache.org/licenses/LICENSE-2.0)

## Reference

[libpnmio](https://github.com/nkkav/libpnmio)
