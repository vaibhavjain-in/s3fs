<?php

namespace Drupal\s3fs;

/**
 * Interface for S3fsStreamOverrideManager.
 */
interface S3fsStreamOverrideManagerInterface {

  /**
   * Dumps an (optimized) asset to S3FS storage.
   *
   * @param string $data
   *   An (optimized) asset's contents.
   * @param string $file_extension
   *   The file extension of this asset.
   *
   * @return string
   *   An URI to access the dumped asset.
   */
  public function dump($data, $file_extension);
}