APP_ABI := arm64-v8a
APP_PLATFORM := android-30
APP_STL := c++_static
APP_OPTIM := release
APP_SHORT_COMMANDS := true

APP_LDFLAGS := -flto -Wl,--gc-sections -s
