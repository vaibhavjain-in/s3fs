<?php

/**
 * @file
 * This file contains no working PHP code; it exists to provide additional
 * documentation for doxygen as well as to document hooks in the standard
 * Drupal manner.
 */

/**
 * @defgroup s3fs_hooks S3 File System hooks
 * Hooks that can be implemented by other modules to extend S3 File System.
 */

/**
 * Allows other modules to change the format and options used when
 * creating an external URL. For example the URL can be a URL directly to the
 * file, or can be a URL to a torrent. In addition, it can be authenticated
 * (time limited), and in that case a save-as can be forced.
 *
 * @param string $local_path
 *   The local filesystem path.
 * @param array $info
 *   Array of keyed elements:
 *     - 'download_type': either 'http' or 'torrent'.
 *     - 'torrent': (boolean) Triggers use of an authenticated URL (w/ timeout)
 *     - 'presigned_url_timeout': (boolean) Time in seconds before an
 *          authenticated URL will time out.
 *     - 'response': array of additional options as described at
 *       http://docs.amazonwebservices.com/AWSSDKforPHP/latest/index.html#m=AmazonS3/get_object_url
 *
 * @return array
 *   The modified array of configuration items.
 */
function hook_s3fs_url_info($local_path, &$info) {
  if ($local_path == 'myfile.jpg') {
    $info['presigned_url'] = TRUE;
    $info['presigned_url_timeout'] = 10;
  }
  return $info;
}
