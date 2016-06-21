<?php

namespace Drupal\s3fs;

use Drupal\Core\Database\Connection;
use Drupal\Core\Database\SchemaObjectExistsException;
use Aws\S3\S3Client;
use Aws\S3\Exception;
use Drupal\Core\File\FileSystem;

/**
 * Class ValidateService.
 *
 * @package Drupal\s3fs
 */
class ValidateService implements ValidateServiceInterface {

  /**
   * The database connection.
   *
   * @var \Drupal\Core\Database\Connection
   */
  protected $connection;

  /**
   * Constructs a new Database connection
   */
  public function __construct(Connection $connection) {
    $this->connection = $connection;
  }

  /**
   * Validate the S3fs config
   *
   * @param $config
   *   Array of configuration settings from which to configure the client.
   * @param $returnError
   *   Boolean, False by default.
   *
   * @return Boolean/array
   */
  public function validate(array $config, $returnError = FALSE) {
    if (!empty($config['use_customhost']) && empty($config['hostname'])) {
      if ($returnError) {
        $name = 'hostname';
        $msg = 'You must specify a Hostname to use the Custom Host feature.';
        return [$name, $msg];
      }
      return FALSE;
    }
    if (!empty($config['use_cname']) && empty($config['domain'])) {
      if ($returnError) {
        $name = 'domain';
        $msg = 'You must specify a CDN Domain Name to use the CNAME feature.';
        return [$name, $msg];
      }
      return FALSE;
    }

    try {
      $s3 = $this->getAmazonS3Client($config);
    }
    catch (Exception\S3Exception $e) {
      if ($returnError) {
        $name = 'form';
        $msg = $e->getMessage();
        return [$name, $msg];
      }
      return FALSE;
    }

    // Test the connection to S3, and the bucket name.
    try {
      // listObjects() will trigger descriptive exceptions if the credentials,
      // bucket name, or region are invalid/mismatched.
      $s3->listObjects(array('Bucket' => $config['bucket'], 'MaxKeys' => 1));
    }
    catch (Exception\S3Exception $e) {
      if ($returnError) {
        $name = 'form';
        $msg = 'An unexpected error occurred. ' . $e->getMessage();
        return [$name, $msg];
      }
      return FALSE;
    }

    return TRUE;
  }

  /**
   * Sets up the S3Client object.
   * For performance reasons, only one S3Client object will ever be created
   * within a single request.
   *
   * @param $config
   *   Array of configuration settings from which to configure the client.
   *
   * @return \Aws\S3\S3Client
   *   The fully-configured S3Client object.
   *
   * @throws \Drupal\s3fs\S3fsException
   */
  public function getAmazonS3Client($config) {
    $s3 = drupal_static(__FUNCTION__);
    $static_config = drupal_static(__FUNCTION__);

    // If the client hasn't been set up yet, or the config given to this call is
    // different from the previous call, (re)build the client.
    if (!isset($s3) || $static_config != $config) {
      $savedConfig = \Drupal::config('s3fs.settings');
      // For the SDK credentials, get the saved settings from _s3fs_get_setting(). But since $config might be populated
      // with to-be-validated settings, its contents (if set) override the saved settings.
      $access_key = $savedConfig->get['access_key'];
      if (isset($config['access_key'])) {
        $access_key = $config['access_key'];
      }
      $secret_key = $savedConfig->get['secret_key'];
      if (isset($config['secret_key'])) {
        $secret_key = $config['secret_key'];
      }
      $use_instance_profile = $savedConfig->get['use_instance_profile'];
      if (isset($config['use_instance_profile'])) {
        $use_instance_profile = $config['use_instance_profile'];
      }
      $default_cache_config = $savedConfig->get['default_cache_config'];
      if (isset($config['default_cache_config'])) {
        $default_cache_config = $config['default_cache_config'];
      }

      if (!class_exists('Aws\S3\S3Client')) {
        throw new S3fsException(
          t('Cannot load Aws\S3\S3Client class. Please ensure that the awssdk2 library is installed correctly.')
        );
      }
      else if (!$use_instance_profile && (!$secret_key || !$access_key)) {
        throw new S3fsException(t("Your AWS credentials have not been properly configured.
        Please set them on the S3 File System admin/config/media/s3fs page or
        set \$conf['awssdk2_access_key'] and \$conf['awssdk2_secret_key'] in settings.php."));
      }
      else if ($use_instance_profile && empty($default_cache_config)) {
        throw new s3fsException(t("Your AWS credentials have not been properly configured.
        You are attempting to use instance profile credentials but you have not set a default cache location.
        Please set it on the admin/config/media/s3fs page or set \$conf['awssdk2_default_cache_config'] in settings.php."));
      }

      // Create the Aws\S3\S3Client object.
      if ($use_instance_profile) {
        $client_config = array('default_cache_config' => $default_cache_config);
      }
      else {
        $client_config['credentials'] = array(
          'key'    => $access_key,
          'secret' => $secret_key,
        );
      }
      if (!empty($config['region'])) {
        $client_config['region'] = $config['region'];
        // Signature v4 is only required in the Beijing and Frankfurt regions.
        // Also, setting it will throw an exception if a region hasn't been set.
        $client_config['signature'] = 'v4';
      }
      if (!empty($config['use_customhost']) && !empty($config['hostname'])) {
        $client_config['base_url'] = $config['hostname'];
      }
      $client_config['version'] = 'latest';
      $s3 = S3Client::factory($client_config);
    }
    $static_config = $config;
    return $s3;
  }

  /**
   * Refreshes the metadata cache.
   *
   * Iterates over the full list of objects in the s3fs_root_folder within S3
   * bucket (or the entire bucket, if no root folder has been set), caching
   * their metadata in the database.
   *
   * It then caches the ancestor folders for those files, since folders are not
   * normally stored as actual objects in S3.
   *
   * @param array $config
   *   An s3fs configuration array.
   */
  function refreshCache($config) {
    // Bomb out with an error if our configuration settings are invalid.
    if (!$this->validate($config)) {
      // @todo: This would be used when refreshing caches from Interface.
      //form_set_error('s3fs_refresh_cache][refresh', t('Unable to validate S3 configuration settings.'));
      return;
    }
    $s3 = $this->getAmazonS3Client($config);

    // Set up the iterator that will loop over all the objects in the bucket.
    $file_metadata_list = array();
    $iterator_args = array('Bucket' => $config['bucket']);
    if (!empty($config['root_folder'])) {
      // If the root_folder option has been set, retrieve from S3 only those files
      // which reside in the root folder.
      $iterator_args['Prefix'] = "{$config['root_folder']}/";
    }
    $iterator = $s3->getListObjectVersionsIterator($iterator_args);
    // NOTE: Setting the maximum page size lower than 1000 will have no effect,
    // as stated by the API docs.
    $iterator->setPageSize(1000);

    // The $folders array is an associative array keyed by folder paths, which
    // is constructed as each filename is written to the DB. After all the files
    // are written, the folder paths are converted to metadata and written.
    $folders = array();
    // Start by gathering all the existing folders. If we didn't do this, empty
    // folders would be lost, because they'd have no files from which to rebuild
    // themselves.
    $existing_folders = $this->connection->select('s3fs_file', 's')
      ->fields('s', array('uri'))
      ->condition('dir', 1);
    foreach ($existing_folders->execute()->fetchCol(0) as $folder_uri) {
      $folders[$folder_uri] = TRUE;
    }

    // Create the temp table, into which all the refreshed data will be written.
    // After the full refresh is complete, the temp table will be swapped with
    // the real one.
    module_load_install('s3fs');
    $schema = s3fs_schema();
    try {
      $this->connection->schema()->createTable('s3fs_file_temp', $schema['s3fs_file']);
    }
    catch (SchemaObjectExistsException $e) {
      // The table already exists, so we can simply truncate it to start fresh.
      $this->connection->truncate('s3fs_file_temp')->execute();
    }

    // Set up an event listener to consume each page of results before the next
    // request is made.
    $dispatcher = $iterator->getEventDispatcher();
    $dispatcher->addListener('resource_iterator.before_send', function($event) use (&$file_metadata_list, &$folders) {
      $this->writeMetadata($file_metadata_list, $folders);
    });

    foreach ($iterator as $s3_metadata) {
      $key = $s3_metadata['Key'];
      // The root folder is an implementation detail that only appears on S3.
      // Files' URIs are not aware of it, so we need to remove it beforehand.
      if (!empty($config['root_folder'])) {
        $key = str_replace("{$config['root_folder']}/", '', $key);
      }

      // Figure out the scheme based on the key's folder prefix.
      $public_folder_name = !empty($config['public_folder']) ? $config['public_folder'] : 's3fs-public';
      $private_folder_name = !empty($config['private_folder']) ? $config['private_folder'] : 's3fs-private';
      if (strpos($key, "$public_folder_name/") === 0) {
        // Much like the root folder, the public folder name must be removed from URIs.
        $key = str_replace("$public_folder_name/", '', $key);
        $uri = "public://$key";
      }
      else if (strpos($key, "$private_folder_name/") === 0) {
        $key = str_replace("$private_folder_name/", '', $key);
        $uri = "private://$key";
      }
      else {
        // No special prefix means it's an s3:// file.
        $uri = "s3://$key";
      }

      if ($uri[strlen($uri) - 1] == '/') {
        // Treat objects in S3 whose filenames end in a '/' as folders.
        // But don't store the '/' itself as part of the folder's uri.
        $folders[rtrim($uri, '/')] = TRUE;
      }
      else {
        // Only store the metadata for the latest version of the file.
        if (isset($s3_metadata['IsLatest']) && !$s3_metadata['IsLatest']) {
          continue;
        }
        // Files with no StorageClass are actually from the DeleteMarkers list,
        // rather then the Versions list. They represent a file which has been
        // deleted, so don't cache them.
        if (!isset($s3_metadata['StorageClass'])) {
          continue;
        }
        // Buckets with Versioning disabled set all files' VersionIds to "null".
        // If we see that, unset VersionId to prevent "null" from being written
        // to the DB.
        if (isset($s3_metadata['VersionId']) && $s3_metadata['VersionId'] == 'null') {
          unset($s3_metadata['VersionId']);
        }
        $file_metadata_list[] = _s3fs_convert_metadata($uri, $s3_metadata);
      }
    }

    // The event listener doesn't fire after the last page is done, so we have
    // to write the last page of metadata manually.
    $this->writeMetadata($file_metadata_list, $folders);

    // Now that the $folders array contains all the ancestors of every file in
    // the cache, as well as the existing folders from before the refresh,
    // write those folders to the DB.
    if ($folders) {
      $insert_query = $this->connection->insert('s3fs_file_temp')
        ->fields(array('uri', 'filesize', 'timestamp', 'dir', 'version'));
      foreach ($folders as $folder_uri => $ph) {
        $metadata = _s3fs_convert_metadata($folder_uri, array());
        $insert_query->values($metadata);
      }
      // TODO: If this throws an integrity constraint violation, then the user's
      // S3 bucket has objects that represent folders using a different scheme
      // than the one we account for above. The best solution I can think of is
      // to convert any "files" in s3fs_file_temp which match an entry in the
      // $folders array (which would have been added in writeMetadata())
      // to directories.
      $insert_query->execute();
    }

    // Swap the temp table with the real table.
    $this->connection->schema()->renameTable('s3fs_file', 's3fs_file_old');
    $this->connection->schema()->renameTable('s3fs_file_temp', 's3fs_file');
    $this->connection->schema()->dropTable('s3fs_file_old');

    // Clear every s3fs entry in the Drupal cache.
    \Drupal::cache(S3FS_CACHE_BIN)->deleteAll();
    cache_clear_all(S3FS_CACHE_PREFIX, S3FS_CACHE_BIN, TRUE);

    drupal_set_message(t('S3 File System cache refreshed.'));
  }

  /**
   * Writes metadata to the temp table in the database.
   *
   * @param array $file_metadata_list
   *   An array passed by reference, which contains the current page of file
   *   metadata. This function empties out $file_metadata_list at the end.
   * @param array $folders
   *   An associative array keyed by folder name, which is populated with the
   *   ancestor folders of each file in $file_metadata_list.
   */
  function writeMetadata(&$file_metadata_list, &$folders) {
    if ($file_metadata_list) {
      $insert_query = $this->connection->insert('s3fs_file_temp')
        ->fields(array('uri', 'filesize', 'timestamp', 'dir', 'version'));

      foreach ($file_metadata_list as $metadata) {
        // Write the file metadata to the DB.
        $insert_query->values($metadata);

        // Add the ancestor folders of this file to the $folders array.
        $uri = FileSystem::dirname($metadata['uri']);
        $root = file_uri_scheme($uri) . '://';
        // Loop through each ancestor folder until we get to the root uri.
        while ($uri != $root) {
          $folders[$uri] = TRUE;
          $uri = FileSystem::dirname($uri);
        }
      }
      $insert_query->execute();
    }

    // Empty out the file array, so it can be re-filled by the next request.
    $file_metadata_list = array();
  }
}
