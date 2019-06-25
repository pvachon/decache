# `decache`: Pull Mach-O Objects out of `dyld_shared_cache`

A handy tool to pull object files out of the `dyld_shared_cache` for iOS.

I only have tested this with the iOS 12.3 `dyld_shared_cache`, so expect bugs if
you're using older caches. As well, this only supports 64-bit Mach-O objects,
so if you're using something else, roll up your sleeves and start patching.

## Usage
 * `./decache -D [dyld_shared_cache file]` - Dump a directory, to stdout, of the Mach-O objects in the cache
 * `./decache [dyld_shared_cache file] [desired shared object] [output file]` - Extract the desired Mach-O object from the `dyld_shared_cache`.

## Building
 * Just type `make`. There shouldn't be external dependencies.

## Why would you do this?
Why not?
