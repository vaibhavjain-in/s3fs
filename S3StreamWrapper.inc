<?php

/**
 * @file
 * Drupal stream wrapper implementation for S3 File System.
 *
 * Implements DrupalStreamWrapperInterface to provide an Amazon S3 wrapper with
 * the s3:// prefix.
 */
class S3StreamWrapper implements DrupalStreamWrapperInterface {
  
  /**
   * @var string Instance URI referenced as "s3://key"
   */
  protected $uri;
  
  /**
   * @var AmazonS3 S3 client object from the AWS SDK, shared across all
   * instances of S3StreamWrapper.
   */
  protected static $s3_client = null;
  
  /**
   * @var AmazonS3 An alias for self::$s3_client.
   */
  protected $s3 = null;
  
  /**
   * @var string S3 bucket name.
   */
  protected $bucket;
  
  /**
   * @var string Domain we use to access files over http.
   */
  protected $domain = NULL;
  
  /**
   * @var int Current read/write position.
   */
  protected $position = 0;
  
  /**
   * @var int Total size of the object as returned by S3 (Content-Length)
   */
  protected $object_size = 0;
  
  /**
   * @var string Object read/write buffer, typically a file.
   */
  protected $buffer = NULL;
  
  /**
   * @var boolean Whether $buffer is active as a write buffer.
   */
  protected $buffer_write = FALSE;
  
  /**
   * @var int Buffer length.
   */
  protected $buffer_length = 0;
  
  /**
   * Records the number of calls to stream_write() between each call to
   * stream_flush(), for testing purposes.
   */
  protected $sw_call_count = 0;
    
  /**
   * @var array directory listing.
   */
  protected $dir = NULL;
  
  /**
   * @var array Map for files that should be delivered with a torrent URL.
   */
  protected $torrents = array();
  
  /**
   * @var array Map for files that should have their Content-Disposition header
   * set to force "save as".
   */
  protected $saveas = array();
  
  /**
   * @var array Map for files that should be created with a URL that times
   * out in a designated number of seconds.
   */
  protected $presigned_urls = array();
  
  /**
   * @var boolean The constructor sets this to TRUE once it's finished.
   * See the comment for _assert_constructor_called() for why this exists.
   */
  protected $constructed = FALSE;
  
  /**
   * @var array Default map for determining file mime types.
   */
  protected static $mime_type_mapping = NULL;
  
  /**
   * Static function to determine a file's media type.
   *
   * Uses Drupal's mimetype mapping, unless a different mapping is specified.
   *
   * @return
   *   Returns a string representing the file's MIME type, or
   *   'application/octet-stream' if no type cna be determined.
   */
  public static function getMimeType($uri, $mapping=NULL) {
    self::_test_log("getMimeType($uri, $mapping) called.");
    // Load the default mime type map.
    if (!isset(self::$mime_type_mapping)) {
      include_once DRUPAL_ROOT . '/includes/file.mimetypes.inc';
      self::$mime_type_mapping = file_mimetype_mapping();
    }
    
    // If a mapping wasn't specified, use the default map.
    if ($mapping == NULL) {
      $mapping = self::$mime_type_mapping;
    }
    
    $extension = '';
    $file_parts = explode('.', basename($uri));
    
    // Remove the first part: a full filename should not match an extension.
    array_shift($file_parts);
    
    // Iterate over the file parts, trying to find a match.
    // For my.awesome.image.jpeg, we try:
    //   - jpeg
    //   - image.jpeg
    //   - awesome.image.jpeg
    while ($additional_part = array_pop($file_parts)) {
      $extension = strtolower($additional_part . ($extension ? '.' . $extension : ''));
      if (isset($mapping['extensions'][$extension])) {
        return $mapping['mimetypes'][$mapping['extensions'][$extension]];
      }
    }
    
    // No mime types matches, so return the default.
    return 'application/octet-stream';
  }
  
  /**
   * Object constructor.
   *
   * Creates the AmazonS3 client object and activates the options specified on
   * the S3 File System admin page.
   */
  public function __construct() {
    $this->bucket = $bucket = variable_get('s3fs_bucket', '');
    
    // If it hasn't already been done in this request, load the AWSSDK library
    // and create the AmazonS3 client.
    if (!isset(self::$s3_client)) {
      if (!libraries_load('awssdk')) {
        throw new Exception(t('Unable to load the AWS SDK. Please check you have installed the library correctly and configured your S3 credentials.'));
      }
      else if (empty($this->bucket)) {
        throw new Exception(t('AmazonS3 bucket name not configured. Please visit the !config_page.',
          array('!config_page' => l(t('configuration page'), '/admin/config/media/s3fs'))));
      }
      else {
        try {
         self::$s3_client = new AmazonS3();
         // Using SSL slows down uploads significantly, but it's unsafe to disable it.
         // I'm still looking for a better solution. -- coredumperror 2013/07/12
         //self::$s3_client->disable_ssl();
         $this->_test_log('Created AmazonS3 client.');
        }
        catch (Exception $e) {
          throw new Exception(t('There was a problem connecting to S3: @msg', array('@msg' => $e->getMessage())));
        }
      }
    }
    $this->s3 = self::$s3_client;
    
    global $is_https;
    $scheme = $is_https ? 'https' : 'http';
    // CNAME support for customizing S3 URLs.
    if (variable_get('s3fs_cname', FALSE)) {
      $domain = variable_get('s3fs_domain', '');
      if ($domain) {
        $this->domain = "$scheme://$domain";
      }
      else {
        $this->domain = "$scheme://{$this->bucket}";
      }
    }
    else {
      $this->domain = "$scheme://{$this->bucket}.s3.amazonaws.com";
    }
    
    // Torrent list
    $torrents = explode("\n", variable_get('s3fs_torrents', ''));
    $torrents = array_map('trim', $torrents);
    $torrents = array_filter($torrents, 'strlen');
    $this->torrents = $torrents;
    
    // Presigned url-list
    $presigned_urls = explode("\n", variable_get('s3fs_presigned_urls', ''));
    $presigned_urls = array_map('trim', $presigned_urls);
    $presigned_urls = array_filter($presigned_urls, 'strlen');
    $this->presigned_urls = array();
    foreach ($presigned_urls as $presigned_url) {
      // Check for an explicit key.
      $matches = array();
      if (preg_match('/(.*)\|(.*)/', $presigned_url, $matches)) {
        $this->presigned_urls[$matches[2]] = $matches[1];
      }
      else {
        $this->presigned_urls[$presigned_url] = 60;
      }
    }
    
    // Force "save as" list
    $saveas = explode("\n", variable_get('s3fs_saveas', ''));
    $saveas = array_map('trim', $saveas );
    $saveas = array_filter($saveas , 'strlen');
    $this->saveas = $saveas;
    
    $this->constructed = TRUE;
    $this->_test_log('S3StreamWrapper constructed.');
  }
  
  /**
   * Sets the stream resource URI.
   *
   * URIs are formatted as "s3://key".
   *
   * @return
   *   Returns the current URI of the instance.
   */
  public function setUri($uri) {
    $this->_test_log("setUri($uri) called.");
    $this->uri = $uri;
  }
  
  /**
   * Returns the stream resource URI.
   *
   * URIs are formatted as "s3://key".
   *
   * @return
   *   Returns the current URI of the instance.
   */
  public function getUri() {
    $this->_test_log('getUri() called.');
    return $this->uri;
  }
  
  /**
   * Returns a web accessible URL for the resource.
   *
   * In the format http://mybucket.s3.amazonaws.com/key, or something different
   * as configured in the admin settings.
   *
   * @return
   *   Returns a string containing a web accessible URL for the resource.
   */
  public function getExternalUrl() {
    $this->_test_log('getExternalUri() called.');
    $s3_filename = $this->_uri_to_s3_filename($this->uri);
    
    // Image styles support:
    // If an image derivative URL (e.g. styles/thumbnail/blah.jpg) is requested
    // and the file doesn't exist, provide a URL to s3fs's special version of
    // image_style_deliver(), which will create the derivative when that URL
    // gets requested.
    $path_parts = explode('/', $s3_filename);
    if ($path_parts[0] == 'styles') {
      if (!$this->_s3fs_get_object($this->uri)) {
        array_shift($path_parts);
        return url('s3/files/styles/' . implode('/', $path_parts), array('absolute' => true));
      }
    }
    
    $info = array(
      'download_type' => 'http',
      'presigned_url' => FALSE,
      'presigned_url_timeout' => 60,
      'response' => array(),
    );
    
    // Allow other modules to change the download link type.
    $info = array_merge($info, module_invoke_all('s3fs_url_info', $s3_filename, $info));
    
    ///////////////
    // UI overrides
    ///////////////
    // Torrent URLs
    if ($info['download_type'] != 'torrent') {
      foreach ($this->torrents as $path) {
        if (preg_match('#' . strtr($path, '#', '\#') . '#', $s3_filename)) {
          $info['download_type'] = 'torrent';
          break;
        }
      }
    }
    // Presigned URLs
    if (!$info['presigned_url']) {
      foreach ($this->presigned_urls as $path => $timeout) {
        if (preg_match('#' . strtr($path, '#', '\#') . '#', $s3_filename)) {
          $info['presigned_url'] = TRUE;
          $info['presigned_url_timeout'] = $timeout;
          break;
        }
      }
    }
    // Save as
    if ($info['download_type'] != 'torrent') {
      foreach ($this->saveas as $path) {
        if (preg_match('#' . strtr($path, '#', '\#') . '#', $s3_filename)) {
          $filename = basename($s3_filename);
          $info['response']['content-disposition'] = "attachment; filename=\"$filename\"";
          break;
        }
      }
    }
    
    $timeout = $info['presigned_url'] ? time() + $info['presigned_url_timeout'] : 0;
    $torrent = ($info['download_type'] == 'torrent') ? TRUE : FALSE;
    $response = $info['presigned_url'] ? $info['response'] : array();
    if ($info['presigned_url'] || $info['download_type'] != 'http' || !empty($info['response'])) {
      $url = $this->s3->get_object_url($this->bucket, $s3_filename, $timeout, array('torrent' => $torrent, 'response' => $response));
      return $url;
    }
    
    $url = "{$this->domain}/$s3_filename";
    return $url;
  }
  
  /**
   * Changes permissions of the resource.
   * This wrapper doesn't support the concept of filesystem permissions.
   *
   * @param $mode
   *   Integer value for the permissions. Consult PHP chmod() documentation
   *   for more information.
   *
   * @return
   *   Returns TRUE.
   */
  public function chmod($mode) {
    $this->_assert_constructor_called();
    $this->_test_log("chmod($mode) called.");
    return TRUE;
  }
  
  /**
   * Returns canonical, absolute path of the resource.
   * This wrapper does not support realpath().
   *
   * @return
   *   Returns FALSE.
   */
  public function realpath() {
    $this->_test_log("realpath() called.");
    return FALSE;
  }
  
  /**
   * Gets the name of the directory from a given path.
   *
   * This method is usually accessed through drupal_dirname(), which wraps
   * around the normal PHP dirname() function, which does not support stream
   * wrappers.
   *
   * @param $uri
   *   An optional URI.
   *
   * @return
   *   A string containing the directory name, or FALSE if not applicable.
   *
   * @see drupal_dirname()
   */
  public function dirname($uri=NULL) {
    $this->_test_log("dirname($uri) called.");
    if (!isset($uri)) {
      $uri = $this->uri;
    }
    $target = $this->getTarget($uri);
    $dirname = dirname($target);
    
    // Special case for calls to dirname('s3://'), ensuring that recursive
    // calls eventually bottom out.
    if ($dirname == '.') {
      $dirname = '';
    }
    return "s3://$dirname";
  }
  
  /**
   * Support for fopen(), file_get_contents(), file_put_contents() etc.
   *
   * @param $uri
   *   A string containing the URI of the file to open.
   * @param $mode
   *   The file mode ("r", "wb" etc.).
   * @param $options
   *   A bit mask of STREAM_USE_PATH and STREAM_REPORT_ERRORS.
   * @param $opened_path
   *   A string containing the path actually opened.
   *
   * @return
   *   Returns TRUE if file was opened successfully.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-open.php
   */
  public function stream_open($uri, $mode, $options, &$opened_path) {
    $this->_test_log("stream_open($uri, $mode, $options, $opened_path) called.");
    $this->uri = $uri;
    
    // If this stream is being opened for writing, clear the object buffer
    // We'll create the object during stream_flush().
    if (strpbrk($mode, 'wax')) {
      $this->_clear_buffer();
      $this->write_buffer = TRUE;
      return TRUE;
    }
    
    $metadata = $this->_s3fs_get_object($uri);
    if ($metadata) {
      $this->_clear_buffer();
      $this->write_buffer = false;
      $this->object_size = $metadata['filesize'];
      return TRUE;
    }
    
    return FALSE;
  }
  
  /**
   * Support for fclose().
   *
   * Clears the object buffer.
   *
   * @return
   *   TRUE
   *
   * @see http://php.net/manual/en/streamwrapper.stream-close.php
   */
  public function stream_close() {
    $this->_test_log("stream_close() called.");
    $this->_clear_buffer();
    return TRUE;
  }
  
  /**
   * Support for flock().
   * flock() is not supported at this time.
   *
   * @param $operation
   *   One of the following:
   *   - LOCK_SH to acquire a shared lock (reader).
   *   - LOCK_EX to acquire an exclusive lock (writer).
   *   - LOCK_UN to release a lock (shared or exclusive).
   *   - LOCK_NB if you don't want flock() to block while locking (not
   *     supported on Windows).
   *
   * @return
   *   returns FALSE.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-lock.php
   */
  public function stream_lock($operation) {
    $this->_test_log("stream_lock($operation) called.");
    return FALSE;
  }
  
  /**
   * Support for fread(), file_get_contents() etc.
   *
   * @param $count
   *   Maximum number of bytes to be read.
   *
   * @return
   *   The string that was read, or FALSE in case of an error.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-read.php
   */
  public function stream_read($count) {
    $this->_test_log("stream_read($count) called.");
    // Make sure that count doesn't exceed object size
    if ($count + $this->position > $this->object_size) {
      $count = $this->object_size - $this->position;
    }
    $data = '';
    if ($count > 0) {
      $range_end = $this->position + $count - 1;
      if ($range_end > $this->buffer_length) {
        $opts = array(
          'range' => "{$this->position}-$range_end",
        );
        $response = $this->s3->get_object($this->bucket, $this->_uri_to_s3_filename($this->uri), $opts);
        if ($response->isOK()) {
          $this->buffer .= $response->body;
          $this->buffer_length += strlen($response->body);
        }
      }
      if (isset($response)) {
        $data = substr($response->body, 0, min($count, $this->object_size));
        $this->position += strlen($data);
      }
    }
    return $data;
  }
  
  /**
   * Support for fwrite(), file_put_contents() etc.
   *
   * @param $data
   *   The string to be written.
   *
   * @return
   *   The number of bytes written (integer).
   *
   * @see http://php.net/manual/en/streamwrapper.stream-write.php
   */
  public function stream_write($data) {
    $this->sw_call_count++;
    
    $data_length = strlen($data);
    $this->buffer .= $data;
    $this->buffer_length += $data_length;
    $this->position += $data_length;
    return $data_length;
  }
  
  /**
   * Support for feof().
   *
   * @return
   *   TRUE if end-of-file has been reached.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-eof.php
   */
  public function stream_eof() {
    $this->_test_log("stream_eof() called.");
    if (!$this->uri) {
      return TRUE;
    }
    return ($this->position >= $this->object_size);
  }
  
  /**
   * Support for fseek().
   *
   * @param $offset
   *   The byte offset to got to.
   * @param $whence
   *   SEEK_SET, SEEK_CUR, or SEEK_END.
   *
   * @return
   *   TRUE on success.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-seek.php
   */
  public function stream_seek($offset, $whence) {
    $this->_test_log("stream_seek($offset, $whence) called.");
    switch($whence) {
      case SEEK_CUR:
        // Set position to current location plus $offset
        $new_position = $this->position + $offset;
        break;
      case SEEK_END:
        // Set position to eof plus $offset
        $new_position = $this->object_size + $offset;
        break;
      case SEEK_SET:
      default:
        // Set position equal to $offset
        $new_position = $offset;
        break;
    }
    
    $ret = ($new_position >= 0 && $new_position <= $this->object_size);
    if ($ret) {
      $this->position = $new_position;
    }
    return $ret;
  }
  
  /**
   * Support for fflush(). Flush current cached stream data to storage.
   *
   * @return
   *   TRUE if data was successfully stored (or there was no data to store).
   *
   * @see http://php.net/manual/en/streamwrapper.stream-flush.php
   */
  public function stream_flush() {
    $this->_test_log("stream_flush() called after {$this->sw_call_count} calls to stream_write().");
    $this->sw_call_count = 0;
    
    // TODO: To enforce per user space limits, we may want to call file_validate_size() in here before
    // writing to S3.
    if ($this->write_buffer) {
      $opts = array(
        'body' => $this->buffer,
        'acl' => AmazonS3::ACL_PUBLIC,
        'contentType' => S3StreamWrapper::getMimeType($this->uri),
      );
      $response = $this->s3->create_object($this->bucket, $this->_uri_to_s3_filename($this->uri), $opts);
      if ($response->isOK()) {
        // Get the metadata for the file we just wrote.
        $metadata = NULL;
        $s3_metadata = $this->s3->get_object_metadata($this->bucket, $this->_uri_to_s3_filename($this->uri));
        if ($s3_metadata) {
          $metadata = _s3fs_format_metadata($this->uri, $s3_metadata);
          $this->_s3fs_write_cache($metadata);
          return TRUE;
        }
      }
    }
    $this->_clear_buffer();
    return FALSE;
  }
  
  /**
   * Support for ftell().
   *
   * @return
   *   The current offset in bytes from the beginning of file.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-tell.php
   */
  public function stream_tell() {
    $this->_test_log("stream_tell() called.");
    return $this->position;
  }
  
  /**
   * Support for fstat().
   *
   * @return
   *   An array with file status, or FALSE in case of an error - see fstat()
   *   for a description of this array.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-stat.php
   */
  public function stream_stat() {
    $this->_test_log("stream_stat() called.");
    return $this->_stat($this->uri);
  }
  
  /**
   * Support for unlink().
   *
   * @param $uri
   *   A string containing the uri to the resource to delete.
   *
   * @return
   *   TRUE if resource was successfully deleted, regardless of whether or not
   *   the file actually existed.
   *   FALSE if the call to S3 failed, in which case the file will not be
   *   removed from the cache.
   *
   * @see http://php.net/manual/en/streamwrapper.unlink.php
   */
  public function unlink($uri) {
    $this->_assert_constructor_called();
    $this->_test_log("unlink($uri) called.");
    $response = $this->s3->delete_object($this->bucket, $this->_uri_to_s3_filename($uri));
    if ($response->isOK()) {
      $this->_s3fs_delete_cache($uri);
      return TRUE;
    }
    return FALSE;
  }
  
  /**
   * Support for rename().
   *
   * If $to_uri exists, this file will be overwritten. This behavior is
   * identical to the PHP rename() function.
   *
   * @param $from_uri
   *   The uri to the file to rename.
   * @param $to_uri
   *   The new uri for file.
   *
   * @return
   *   TRUE if file was successfully renamed.
   *
   * @see http://php.net/manual/en/streamwrapper.rename.php
   */
  public function rename($from_uri, $to_uri) {
    $this->_assert_constructor_called();
    $this->_test_log("rename() called.");
    $from = $this->_uri_to_s3_filename($from_uri);
    $to = $this->_uri_to_s3_filename($to_uri);
    
    $response = $this->s3->copy_object(
      array('bucket' => $this->bucket, 'filename' => $from),
      array('bucket' => $this->bucket, 'filename' => $to),
      array('acl' => AmazonS3::ACL_PUBLIC)
    );
    
    // If the copy was successful, cache the new file, and remove the original.
    if ($response->isOK()) {
      $metadata = $this->_s3fs_read_cache($from_uri);
      $metadata['uri'] = $to_uri;
      $this->_s3fs_write_cache($metadata);
      // The rename will have failed if the unlink fails.
      return $this->unlink($from_uri);
    }
    return FALSE;
  }
  
  /**
   * Returns the local writable target of the resource within the stream.
   *
   * This function should be used in place of calls to realpath() or similar
   * functions when attempting to determine the location of a file. While
   * functions like realpath() may return the location of a read-only file, this
   * method may return a URI or path suitable for writing that is completely
   * separate from the URI used for reading.
   *
   * @param $uri
   *   Optional URI.
   *
   * @return
   *   Returns a string representing a location suitable for writing of a file,
   *   or FALSE if unable to write to the file such as with read-only streams.
   */
  protected function getTarget($uri=NULL) {
    if (!isset($uri)) {
      $uri = $this->uri;
    }
    $this->_test_log("getTarget($uri) called.");
    $data = explode('://', $uri, 2);
    // Remove erroneous leading or trailing forward-slashes and backslashes.
    return count($data) == 2 ? trim($data[1], '\/') : FALSE;
  }
  
  /**
   * Support for mkdir().
   *
   * @param $uri
   *   A string containing the URI to the directory to create.
   * @param $mode
   *   Permission flags - see mkdir().
   * @param $options
   *   A bit mask of STREAM_REPORT_ERRORS and STREAM_MKDIR_RECURSIVE.
   *
   * @return
   *   TRUE if directory was successfully created.
   *
   * @see http://php.net/manual/en/streamwrapper.mkdir.php
   */
  public function mkdir($uri, $mode, $options) {
    $this->_assert_constructor_called();
    $this->_test_log("mkdir($uri, $mode, $options) called.");
    
    // If this URI already exists in the cache, return TRUE if it's a folder
    // (so that recursive calls won't improperly report failure when they
    // reach an existing ancestor), or FALSE if it's a file (failure).
    $test_metadata = $this->_s3fs_read_cache($uri);
    if ($test_metadata) {
      return (bool)$test_metadata['dir'];
    }
    
    // S3 is a flat file system, with no concept of directories (just files
    // with slashes in their names). To represent folders, we store them in the
    // metadata cache, without creating anything in S3.
    $metadata = _s3fs_format_metadata($uri, array());
    $metadata['timestamp'] = date('U', time());
    $this->_s3fs_write_cache($metadata);
    
    // If the STREAM_MKDIR_RECURSIVE option was specified, also create all the
    // ancestor folders of this uri.
    $parent_dir = drupal_dirname($uri);
    if (($options & STREAM_MKDIR_RECURSIVE) && $parent_dir != 's3://') {
      return $this->mkdir($parent_dir, $mode, $options);
    }
    return TRUE;
  }
  
  /**
   * Support for rmdir().
   *
   * @param $uri
   *   A string containing the URI to the directory to delete.
   * @param $options
   *   A bit mask of STREAM_REPORT_ERRORS.
   *
   * @return
   *   TRUE if directory was successfully removed.
   *   FALSE if the directory was not empty.
   *
   * @see http://php.net/manual/en/streamwrapper.rmdir.php
   */
  public function rmdir($uri, $options) {
    $this->_assert_constructor_called();
    $this->_test_log("rmdir($uri, $options) called.");
    if (!$this->_s3fs_is_dir($uri)) {
      return FALSE;
    }
    
    if ($uri[strlen($uri)-1] != '/') {
      // If it needs one, add a trailing '/' to the URI, to differentiate
      // from files with this folder's name as a substring.
      // e.g. rmdir('s3://foo/bar') should not care about the existence of
      // 's3://foo/barbell.jpg'.
      $uri .= '/';
    }
    
    $query = db_query("SELECT * FROM {s3fs_file} WHERE uri LIKE :folder", array(':folder' => "$uri%"));
    $files = $query->fetchAll();
    if (empty($files)) {
      // This folder is empty, so it's elegible for deletion.
      $result = db_delete('s3fs_file')
        ->condition('uri', rtrim($uri, '/'), '=')
        ->execute();
      return (bool)$result;
    }
    return FALSE;
  }
  
  /**
   * Support for stat().
   *
   * @param $uri
   *   A string containing the URI to get information about.
   * @param $flags
   *   A bit mask of STREAM_URL_STAT_LINK and STREAM_URL_STAT_QUIET.
   *
   * @return
   *   An array with file status, or FALSE in case of an error - see fstat()
   *   for a description of this array.
   *
   * @see http://php.net/manual/en/streamwrapper.url-stat.php
   */
  public function url_stat($uri, $flags) {
    $this->_assert_constructor_called();
    $this->_test_log("url_stat($uri, $flags) called.");
    return $this->_stat($uri);
  }
  
  /**
   * Support for opendir().
   *
   * @param $uri
   *   A string containing the URI to the directory to open.
   * @param $options
   *   A boolean used to enable safe_mode, which this wrapper doesn't support.
   *
   * @return
   *   TRUE on success.
   *
   * @see http://php.net/manual/en/streamwrapper.dir-opendir.php
   */
  public function dir_opendir($uri, $options=NULL) {
    $this->_assert_constructor_called();
    $this->_test_log("dir_opendir($uri, $options) called.");
    if (!$this->_s3fs_is_dir($uri)) {
      return FALSE;
    }
    
    if ($uri != 's3://') {
      // If this isn't the root folder, add a trailing '/' to differentiate
      // from folders with this folder's name as a substring.
      $uri .= '/';
    }
    
    // Get the list of uris for files and folders which are in the specified
    // folder, but not in any of its subfolders.
    $query = db_query("SELECT uri FROM {s3fs_file} WHERE uri LIKE :folder AND uri NOT LIKE :subfolder",
        array(':folder' => "$uri%", ':subfolder' => "$uri%/%"));
    
    // Create $this->dir as an empty array, since the folder might be empty.
    $this->dir = array();
    foreach ($query->fetchAll(PDO::FETCH_COLUMN, 0) as $uri) {
      $this->dir[] = basename($uri);
    }
    return TRUE;
  }
  
  /**
   * Support for readdir().
   *
   * @return
   *   The next filename, or FALSE if there are no more files in the directory.
   *
   * @see http://php.net/manual/en/streamwrapper.dir-readdir.php
   */
  public function dir_readdir() {
    $this->_test_log("dir_readdir() called.");
    $entry = each($this->dir);
    return $entry ? $entry['value'] : FALSE;
  }
  
  /**
   * Support for rewinddir().
   *
   * @return
   *   TRUE on success.
   *
   * @see http://php.net/manual/en/streamwrapper.dir-rewinddir.php
   */
  public function dir_rewinddir() {
    $this->_test_log("dir_rewinddir() called.");
    reset($this->dir);
    return TRUE;
  }
  
  /**
   * Support for closedir().
   *
   * @return
   *   TRUE on success.
   *
   * @see http://php.net/manual/en/streamwrapper.dir-closedir.php
   */
  public function dir_closedir() {
    $this->_test_log("dir_closedir() called.");
    unset($this->dir);
    return TRUE;
  }
  
  /**
   * Gets the path that the wrapper is responsible for.
   * Even though this function isn't specified in DrupalStreamWrapperInterface,
   * Drupal's code calls it on all wrappers, so we need to define it.
   *
   * @return
   *   String specifying the path.
   */
  public function getDirectoryPath() {
    $this->_test_log("getDirectoryPath() called.");
    return $this->domain;
  }
  
  /**
   * Convert a URI into a valid S3 filename.
   */
  protected function _uri_to_s3_filename($uri) {
    $filename = str_replace('s3://', '', $uri);
    // Remove both leading and trailing /s. S3 filenames never start with /,
    // and a $uri for a folder might be specified with a trailing /, which
    // we'd need to remove to be able to retrieve it from the cache.
    $path = trim($filename, '/');
    return $filename;
  }
  
  /**
   * Flush the object buffers.
   */
  protected function _clear_buffer() {
    $this->position = 0;
    $this->object_size = 0;
    $this->buffer = null;
    $this->buffer_write = false;
    $this->buffer_length = 0;
  }
  
  /**
   * Get the status of the file with the specified URI.
   *
   * @return
   *   An array with file status, or FALSE if the file doesn't exist.
   *   See fstat() for a description of this array.
   *
   * @see http://php.net/manual/en/streamwrapper.stream-stat.php
   */
  protected function _stat($uri) {
    $metadata = $this->_s3fs_get_object($uri);
    if ($metadata) {
      $stat = array();
      $stat[0] = $stat['dev'] = 0;
      $stat[1] = $stat['ino'] = 0;
      $stat[2] = $stat['mode'] = $metadata['mode'];
      $stat[3] = $stat['nlink'] = 0;
      $stat[4] = $stat['uid'] = 0;
      $stat[5] = $stat['gid'] = 0;
      $stat[6] = $stat['rdev'] = 0;
      $stat[7] = $stat['size'] = 0;
      $stat[8] = $stat['atime'] = 0;
      $stat[9] = $stat['mtime'] = 0;
      $stat[10] = $stat['ctime'] = 0;
      $stat[11] = $stat['blksize'] = 0;
      $stat[12] = $stat['blocks'] = 0;
      
      if (!$metadata['dir']) {
        $stat[4] = $stat['uid'] = $metadata['uid'];
        $stat[7] = $stat['size'] = $metadata['filesize'];
        $stat[8] = $stat['atime'] = $metadata['timestamp'];
        $stat[9] = $stat['mtime'] = $metadata['timestamp'];
        $stat[10] = $stat['ctime'] = $metadata['timestamp'];
      }
      return $stat;
    }
    return FALSE;
  }
  
  /**
   * Determine whether the $uri is a directory.
   *
   * @param $uri
   *   A string containing the uri to the resource to check. If none is given
   *   defaults to $this->uri
   *
   * @return
   *   TRUE if the resource is a directory
   */
  protected function _s3fs_is_dir($uri) {
    if ($uri == 's3://' || $uri == 's3:') {
      return TRUE;
    }
    
    // Folders only exist in the cache; they are not stored in S3.
    $metadata = $this->_s3fs_read_cache($uri);
    return $metadata ? $metadata['dir'] : FALSE;
  }
  
  /****************************************************************************
                                 CACHE FUNCTIONS
   ***************************************************************************/
  
  /**
   * Try to fetch an object from the metadata cache. If that file isn't in the
   * cache, it is considered to be nonexistant.
   *
   * @param uri
   *   A string containing the uri of the resource to check.
   *
   *  @return
   *    An array if the $uri exists, otherwise FALSE.
   */
  protected function _s3fs_get_object($uri) {
    // Since this is an internal function, don't log it by default.
    //$this->_test_log("_s3fs_get_object($uri) called.");
    // For the root directory, just return metadata for a generic folder.
    if ($uri == 's3://' || $uri == 's3:') {
      $metadata = _s3fs_format_metadata('/', array(), TRUE);
      return $metadata;
    }
    
    // Trim any trailing '/', in case this is a folder request.
    $uri = rtrim($uri, '/');
    
    // Check if this URI is in the cache.
    $metadata = $this->_s3fs_read_cache($uri);
    
    // If cache ignore is enabled, query S3 for all file requests.
    if (variable_get('s3fs_ignore_cache', FALSE)) {
      // Even when ignoring the cache, we still read folders from it, because
      // they aren't stored in S3.
      if (!empty($metadata['dir'])) {
        return $metadata;
      }
      // Query S3.
      $s3_metadata = $this->s3->get_object_metadata($this->bucket, $this->_uri_to_s3_filename($uri));
      if ($s3_metadata) {
        return _s3fs_format_metadata($uri, $s3_metadata);
      }
      // This URI is neither a cached folder nor in S3: it doesn't exist.
      return FALSE;
    }
    
    return $metadata;
  }
  
  /**
   * Fetch an object from the file metadata cache table.
   *
   * @param uri
   *   A string containing the uri of the resource to check.
   *
   * @return
   *   An array of metadata if the $uri is in the cache, otherwise FALSE.
   */
  protected function _s3fs_read_cache($uri) {
    // Since this is an internal function, don't log it by default.
    //$this->_test_log("_s3fs_read_cache($uri) called.");
    $record = db_query("SELECT * FROM {s3fs_file} WHERE uri = :uri", array(':uri' => $uri))->fetchAssoc();
    return $record ? $record : FALSE;
  }
  
  /**
   * Write an object's metadata to the cache. Also write its ancestor folders
   * to the cache.
   *
   * @param metadata
   *   An associative array of file metadata, in this format:
   *     'uri' => The full URI of the file, including 's3://'.
   *     'filesize' => The size of the file, in bytes.
   *     'timestamp' => The file's create/update timestamp.
   *     'dir' => A boolean indicating whether the object is a directory. THIS IS DEPRECATED.
   *     'mode' => The octal mode of the file.
   *     'uid' => The uid of the owner of the S3 object.
   *
   * @throws
   *   If an exception occurs from the database call, it will percolate out of this function.
   */
  protected function _s3fs_write_cache($metadata) {
    // Since this is an internal function, don't log it by default.
    //$this->_test_log("_s3fs_write_cache({$metadata['uri']}) called.");
    db_merge('s3fs_file')
      ->key(array('uri' => $metadata['uri']))
      ->fields($metadata)
      ->execute();
    
    $dirname = $this->dirname($metadata['uri']);
    if ($dirname != 's3://') {
      $this->mkdir($dirname, NULL, STREAM_MKDIR_RECURSIVE);
    }
  }
  
  /**
   * Delete an object's metadata from the cache.
   *
   * @param uri
   *   The URI of the object to be deleted. Also accepts an array of URIs
   *   to be deleted.
   *
   * @throws
   *   If an exception occurs from the database call, it will percolate out of this function.
   */
  protected function _s3fs_delete_cache($uri) {
    // Since this is an internal function, don't log it by default.
    //$this->_test_log("_s3fs_delete_cache($uri) called.");
    $delete_query = db_delete('s3fs_file');
    if (is_array($uri)) {
      // Build an OR condition to delete all the URIs in one query.
      $or = db_or();
      foreach ($uri as $u) {
        $or->condition('uri', $u, '=');
      }
      $delete_query->condition($or);
    }
    else {
      $delete_query->condition('uri', $uri, '=');
    }
    $delete_query->execute();
  }
  
  /**
   * Call the constructor it it hasn't been has been called yet.
   *
   * Due to PHP bug #40459, the constructor of this class isn't always called
   * for some of the methods.
   *
   * @see https://bugs.php.net/bug.php?id=40459
   */
  protected function _assert_constructor_called() {
    if ($this->constructed === FALSE) {
      $this->__construct();
    }
  }
  
  /**
   * Used for testing.
   */
  private static function _test_log($msg) {
    global $s3fs_testing;
    if ($s3fs_testing) {
      print "TEST: $msg\n";
      flush();
    }
  }
}
