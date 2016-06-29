<?php

namespace Drupal\s3fs;

/**
 * Interface ValidateServiceInterface.
 *
 * @package Drupal\s3fs
 */
interface ValidateServiceInterface {

  /**
   * This will validate the config provided.
   *
   * @param array $config
   * @param bool $returnError
   *
   * @return mixed
   */
  public function validate(array $config, $returnError = FALSE);

}
