# Button Image Support

The context-osk theme system now supports custom images for buttons. Images are automatically scaled to fit the button dimensions.

## Theme File Syntax

Add an `image` property to any button section in your theme file:

```ini
[button]
x=10
y=10
width=64
height=64
keycode=36
label=Enter
image=/path/to/image.png
```

## Supported Image Formats

### 1. File System Path

Simple file path (relative or absolute):

```ini
image=/home/user/.context-osk/icons/enter.png
image=icons/shift.png
```

### 2. File URI

Using the `file://` protocol:

```ini
image=file:///home/user/.context-osk/icons/ctrl.png
```

### 3. Base64 Data URI

Embed images directly in the theme file using base64 encoding:

```ini
image=data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAUA...
```

To convert an image to base64:
```bash
base64 -w 0 myimage.png
```

Then prefix with `data:image/png;base64,`

## Image Behavior

- **Aspect Ratio Preserved**: Images are scaled to fit within the button while maintaining their original aspect ratio
- **Centered with Padding**: Images are centered within the button area with transparent padding
- **Format Support**: PNG format (via Cairo)
- **Transparency Support**: Alpha channel is fully supported for both image content and padding
- **Fallback**: If image loading fails, the button displays using the label text
- **Overlay**: Press feedback overlay works on both image and text buttons

## Example Theme with Images

```ini
[General]
height=100
background_color=#1a1a1a

[Key_Shift_50]
x=2
y=2
width=64
height=64
keycode=50
toggle=true
image=/home/user/.context-osk/icons/shift-arrow.png

[Key_Ctrl_37]
x=70
y=2
width=64
height=64
keycode=37
toggle=true
image=data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAUA...

[Key_Alt_64]
x=138
y=2
width=64
height=64
keycode=64
toggle=true
image=file:///usr/share/icons/alt-key.png
```

## Notes

- Only PNG images are supported currently
- Images with transparency (alpha channel) are fully supported
- The `label` property is still recommended as a fallback identifier
- Image paths are evaluated when the theme is loaded
- Base64 images increase theme file size but make themes portable
