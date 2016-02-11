<?php

/**
 * Implements hook_drush_command().
 */
function large_images_drush_command() {
  $items['large-images-list'] = array(
    'aliases' => array('lil'),
    'description' => 'List large images (by file size) with dimensions',
    'arguments' => array(
      'dir' => 'The directory to operate on',
      'min-size' => 'Only find images greater than this size (8M, 900k, etc.)',
    ),
    'required-arguments' => TRUE,
    'options' => array(
      'maxdepth' => '(--maxdepth=N) Descend at most N levels (default is no limit; use 1 if you want current directory only)',
    ),
    'examples' => array(
      'drush lil . 8M' => 'List files that are larger than 8 MB',
    ),
  );

  $items['large-images-compress'] = array(
    'aliases' => array('lic'),
    'description' => 'Compresses large images and updates the file_managed database table. WARNING: This is NOT reversible. Your image files WILL be overwritten.',
    'arguments' => array(
      'dir' => 'The directory to operate on',
      'min-size' => 'Only compress images greater than this size (8M, 900k, etc.)',
      'quality' => 'Quality to use for compression (1 to 100)',
    ),
    'required-arguments' => TRUE,
    'options' => array(
      'maxdepth' => '(--maxdepth=N) Descend at most N levels (default is no limit; use 1 if you want current directory only)',
      'backup' => '(--backup) Create a backup file (_lgbak) before overwriting the original file',
    ),
    'examples' => array(
      'drush lic . 8M' => 'List files that are larger than 8 MB',
    ),
  );

  return $items;
}

/**
 * This is used by a few different commands
 *
 * @param string $dir
 *   Directory to search
 * @param string $min_size
 *   Minimum size (e.g., 16M or 900K)
 *
 * @return array
 *   Array of lines from drush_shell_exec_output()
 */
function large_images_find_images($dir, $min_size) {
  $full_dir = realpath(drush_cwd() . '/' . $dir);

  if (empty($full_dir)) {
    drush_set_error(dt('Invalid directory: !dir', array('!dir' => $dir)));
    drush_die();
  }

  $depth = drush_get_option('maxdepth', 999999);

  drush_shell_exec('find ' . $full_dir . ' -maxdepth ' . $depth . ' \( -iname "*.jpg" -o -iname "*.jpeg" -o -iname "*.png" \) -type f -size +' . $min_size . ' -exec du -ah {} \; | sort -rh | cut -f 2');

  return drush_shell_exec_output();
}

/**
 * Callback for the large-images-list command
 *
 * @param string $dir
 *   Directory to search
 * @param string $min_size
 *   Minimum size (e.g., 16M or 900k)
 */
function drush_large_images_list($dir, $min_size) {
  $images = large_images_find_images($dir, $min_size);
  if (!empty($images)) {
    foreach ($images as $image) {
      $img_size = getimagesize($image);
      drush_log(dt('!size !wx!h !path', array(
        '!path' => $image,
        '!w' => str_pad($img_size[0], 5, ' ', STR_PAD_LEFT),
        '!h' => str_pad($img_size[1], 5, ' ', STR_PAD_RIGHT),
        '!size' => str_pad(large_images_human_filesize(filesize($image)), 9, ' '),
      )), 'ok');
    }
  }
  else {
    drush_log(dt('No image files found.'), 'ok');
  }
}

/**
 * Callback for the large-images-compress command
 *
 * @param string $dir
 *   Directory to search
 * @param string $min_size
 *   Minimum size (e.g., 16M or 900k)
 * @param string $quality
 *   Quality to use for compression (default is 80)
 */
function drush_large_images_compress($dir, $min_size, $quality) {
  if ($quality > 100 || $quality < 1) {
    drush_set_error('Detected invalid quality value; must be between 1 and 100');
    drush_die();
  }

  if ($quality < 40) {
    if (!drush_confirm(dt('WARNING! Are you sure you want to use such a low quality?'))) {
      drush_user_abort();
      drush_die();
    }
  }

  // Build an array of all managed files
  $result = db_query('SELECT fid, uri FROM file_managed');
  $fids_uris = array();
  if ($result) {
    while ($row = $result->fetchAssoc()) {
      $fids_uris[$row['fid']] = str_replace(array('public://', 'private://'), '', $row['uri']);
    }
  }

  $images = large_images_find_images($dir, $min_size);
  if (!empty($images)) {
    foreach ($images as $image_fullpath) {
      $original_filesize = filesize($image_fullpath);

      // No matter where the executes drush from, we want our pathing to start from the Drupal root directory
      drush_shell_exec('pwd');
      $pwd = drush_shell_exec_output();
      $path_from_drupal_root = ltrim(str_replace($pwd, '', $image_fullpath), '/');

      // Remove "sites/default/files" and similar from the path
      // to get the same type of path we see in the file_managed table's "uri" column
      $public_prefix = variable_get('file_public_path', conf_path() . '/files');
      $private_prefix = variable_get('file_public_path');
      $temp_prefix = variable_get('file_temporary_path');
      $uri_without_scheme = ltrim(str_replace(array($public_prefix, $private_prefix, $temp_prefix), '', $path_from_drupal_root), '/');

      $fids = array();
      foreach ($fids_uris as $fid => $uri) {
        // Check if the uri without the scheme is referenced by file_managed
        if ($uri == $uri_without_scheme) {
          $fids[] = $fid;
        }
      }

      // Compress them image
      if (large_images_compress_image($image_fullpath, $quality)) {
        // If we don't clear the cache, filesize() will return the old size
        clearstatcache(TRUE, $image_fullpath);

        // Update the database
        $fids_status = '';
        if (!empty($fids)) {
          foreach (file_load_multiple($fids) as $file) {
            $file->filesize = filesize($file->uri);
            file_save($file);
          }
          $fids_status = '| db-updated FID #' . implode(', #', $fids);
        }

        drush_log(dt('!path | !old --> !new !fids_status', array(
          '!path' => $path_from_drupal_root,
          '!old' => large_images_human_filesize($original_filesize),
          '!new' => large_images_human_filesize(filesize($image_fullpath)),
          '!fids_status' => $fids_status,
        )), 'ok');
      }
      else {
        drush_set_error(dt('!path | Error compressing file', array('!path' => $path_from_drupal_root)));
      }
    }
  }
  else {
    drush_log(dt('No image files found.'), 'ok');
  }
}

/**
 * Returns a human-readable representation of bytes
 *
 * @param $bytes
 *   How many bytes
 * @param int $decimals
 *   How many decimals to use in the output
 *
 * @return string
 *   Human-readable file size
 */
function large_images_human_filesize($bytes, $decimals = 2) {
  $sz = 'BKMGTP';
  $factor = floor((strlen($bytes) - 1) / 3);
  return sprintf("%.{$decimals}f", $bytes / pow(1024, $factor)) . @$sz[$factor];
}


/**
 * Compresses a JPEG or PNG image
 *
 * @param $filepath
 *   Filepath for the image
 * @param $quality
 *   Quality (default is 80)
 *
 * @return bool
 *   TRUE if successful
 */
function large_images_compress_image($filepath, $quality = 80) {
  $backup = drush_get_option('backup', FALSE);
  if ($backup) {
    copy($filepath, $filepath . '_lgbak');
  }

  $info = getimagesize($filepath);

  if ($info['mime'] == 'image/jpeg') {
    $image = imagecreatefromjpeg($filepath);

    if (imagejpeg($image, $filepath, $quality)) {
      return TRUE;
    }
  }
  elseif ($info['mime'] == 'image/png') {
    $image = imagecreatefrompng($filepath);

    // PNG quality is backwards compared to jpeg.
    // The scale is 0 to 9 (0 being full quality)
    $png_quality = ($quality - 100) / 11.111111;
    $png_quality = round(abs($png_quality));

    if (imagepng($image, $filepath, $png_quality)) {
      return TRUE;
    }
  }

  return FALSE;
}