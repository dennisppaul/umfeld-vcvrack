# Umfeld / VCV Rack / Notes

## Setting up the Development Environment

following instructions from [Plugin Development Tutorial](https://vcvrack.com/manual/PluginDevelopmentTutorial) for macOS:

```zsh
$ brew install git wget cmake autoconf automake libtool jq python zstd pkg-config
$ git clone https://github.com/VCVRack/Rack.git
$ cd Rack
$ git submodule update --init --recursive
$ make dep
$ make
$ export RACK_DIR=~/Documents/dev/Rack
...
$ $RACK_DIR/helper.py createplugin MyPlugin
$ cd MyPlugin
$ make
( save `MyModule.svg.` to folder `res` )
$ $RACK_DIR/helper.py createmodule MyModule res/MyModule.svg src/MyModule.cpp
( modify source )
$ make
$ make dist
( copy plugin from `‌dist` to `~/Library/Application Support/Rack2/` )
```

## Export Panel Designs with Adobe Illustrator (Settings)

- Export As or Artboards Export
- Styling: Inline Style
- Font: Convert to Outlines
- Images: Embed
- Object IDs: Layer Names
- Minify unchecked
- Responsive unchecked
