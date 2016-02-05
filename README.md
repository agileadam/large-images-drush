## Introduction
This provides two drush commands.

1. `large-images-list` will list the largest images (by file size) found in a directory (and its subdirectories by default)
1. `large-images-compress` will compress (shrink file size of) images and update the associated record in Drupal's file_managed table

* Both of these commands require you to specify the directory to search, and the minimum size you'd like to operate on.
* Both of these commands also allow you to specify a maxdepth.
* The compression command takes an additional (required) "quality" argument. This can be anything from 1 to 100 (lowest output quality to highest output quality). For general use the recommended value is somewhere around 80 or 90.
* Run either command with --help to see all options. For example: `drush large-images-list --help`

## Warning

- Run `large-images-list` before running `large-images-compress`
- These commands work fine in the environments where I've used them. This does not mean they'll work perfectly for you! Test before using!
- I recommend you back up your Drupal database and whatever directory you're operating on before executing `large-images-compress`
- `large-images-compress` is not a "smart" command. It simply overwrites the image with the desired output quality and updates the database. It does not look at the current compression level before attempting to generate the new version, though I may add this functionality to prevent trying to go from a lower quality to a higher one.

## Requirements

- Drupal 7 site
- drush
- php with gd library
- `find` command
- `du` command
- `sort` command
- `cut` command

## Example 1
![Example 1](example1.png)

## Example 2
![Example 2](example2.png)

![Example 3](example3.png)
