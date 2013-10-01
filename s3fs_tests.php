#!/opt/local/bin/php
<?php

/**
 * @file
 * Tests for S3FSStreamWrapper and its helper functions.
 * 
 * Drupal's unit testing framework is unsuited for testing this module, because
 * it disallows access to the database. Its web testing framework is
 * somewhat unwieldy as well, since it creates a fresh drupal install to run
 * inside, meaning that the site's file database will be empty, while S3 isn't.
 */

global $s3fs_testing;

function my_assert($condition, $line, $message) {
  if (!$condition) {
    throw new Exception("ASSERT FAILED on Line $line: \"$message\"");
  }
}

function main() {
  if (!file_exists('cron.php')) {
    print "This script must be run from your drupal site's root directory";
    exit(2);
  }
  global $argv, $argc;
  list($filename, $notest) = parse_arguments($argc, $argv);
  
  global $s3fs_testing;
  // Turn on test logging, unless the user said not to.
  if (!$notest) {
    $s3fs_testing = TRUE;
  }
  startUp();
  bootstrap();
  upload_test($filename);
}

function upload_test($filename) {
  $uploaded_uri = "s3://_s3fs_test/test_file.jpg";
  
  my_assert(file_stream_wrapper_valid_scheme('s3'), __LINE__, '"s3" should be a valid stream wrapper scheme.');
  my_assert(file_stream_wrapper_get_class('s3') == 'S3FSStreamWrapper', __LINE__, 'URIs with scheme "s3" should be handled by S3FSStreamWrapper.');
  
  test_print("Uploading: $filename to $uploaded_uri...");
  
  // Load the file into memory.
  $handle = fopen($filename, 'rb');
  test_print("File is " . filesize($filename) . " bytes");
  $file_contents = fread($handle, filesize($filename));
  fclose($handle);
  test_print("File in memory is " . strlen($file_contents) . " bytes");
  
  test_print("Exercising rmdir (to clean up the testing folder)");
  foreach (file_scan_directory('s3://_s3fs_test', '#.*#') as $file) {
    drupal_unlink($file->uri);
  }
  // Don't assert this deletion: we don't care if it fails.
  drupal_rmdir('s3://_s3fs_test');
  my_assert(!is_dir('s3://_s3fs_test'), __LINE__, 'Failed to empty the test directory to begin the test.');
  
  test_print("Exercising mkdir");
  my_assert(drupal_mkdir('s3://_s3fs_test'), __LINE__, 'Failed to mkdir the testing directory on S3.');
  
  test_print("Exercising file write functionality, and several other stream functions which get called because this is an image file.");
  $start_time = time();
  $s3_file = file_save_data($file_contents, $uploaded_uri, FILE_EXISTS_REPLACE);
  $end_time = time();
  $total = $end_time - $start_time;
  test_print("Upload time: $total seconds");
  
  my_assert(file_valid_uri($s3_file->uri), __LINE__, 'The URI to the file we just uploaded should be valid.');
  
  test_print('Exercising the dir_*() functions.');
  $files = file_scan_directory('s3://_s3fs_test', '#.*#');
  my_assert(isset($files[$uploaded_uri]), __LINE__, 'The file we uploaded is not being reported as an element of the testing directory.');
  $file_count = count($files);
  my_assert($file_count == 1, __LINE__, "There should be exactly 1 file in the s3://_s3fs_test directory. Instead, there are $file_count.");
  
  test_print('Exercising getExternalUri().');
  $url = file_create_url($uploaded_uri);
  my_assert($url !== FALSE, __LINE__, 'file_create_url() call failed.');
  test_print('Exercising getExternalUri() for image styles.');
  $url = file_create_url('s3://styles/small_thumbnail/s3/test_image.jpg');
  my_assert(strstr($url, '/system/files/styles') !== FALSE, __LINE__, 'A private filesystem path was not created.');
  
  test_print('Exercising unlink() by moving a file.');
  $moved_uri = 's3://_s3fs_test/test_file2.jpg';
  $ret = file_move($s3_file, $moved_uri, FILE_EXISTS_REPLACE);
  my_assert($ret, __LINE__, 'Failed to move the file to test_file2.jpg');
  $ret = file_exists('s3://_s3fs_test/test_file.jpg');
  my_assert($ret == FALSE, __LINE__, 'The old file should not longer exist.');
  
  test_print('Exercising rename() by moving the file back to the original uri.');
  my_assert(rename($moved_uri, $uploaded_uri), __LINE__, 'Call to rename() failed.');
  
  test_print('Exercising rmdir().');
  my_assert(!drupal_rmdir('s3://_s3fs_test'), __LINE__, 'The rmdir() call should have failed, because s3://_s3fs_test is not empty.');
  my_assert(drupal_unlink($uploaded_uri), __LINE__, 'The delete call for the moved file failed.');
  my_assert(drupal_rmdir('s3://_s3fs_test'), __LINE__, 'The rmdir() call should have succeeded, because s3://_s3fs_test is empty now.');
  my_assert(!is_dir('s3://_s3fs_test'), __LINE__, 's3://_s3fs_test is gone, and should not be considered a directory any more.');
  
  test_print('S3 test complete');
}

function test_print($str) {
  print "$str\n";
}

function startUp() {
  $stdout = fopen('php://stdout', 'w');
  
  ini_set('display_errors', 'TRUE');
  ini_set('display_startup_errors', 'TRUE');
  ini_set('max_execution_time', 18000);
}

function bootstrap() {
  $username = "admin";
  $drupal_base_url = parse_url('http://localhost/');
  
  define('DRUPAL_ROOT', getcwd());
  $_SERVER['HTTP_HOST'] = $drupal_base_url['host'];
  $_SERVER['PHP_SELF'] = $drupal_base_url['path'].'/index.php';
  $_SERVER['REQUEST_URI'] = $_SERVER['SCRIPT_NAME'] = $_SERVER['PHP_SELF'];
  $_SERVER['REMOTE_ADDR'] = NULL;
  $_SERVER['REQUEST_METHOD'] = NULL;
  $_SERVER['SCRIPT_NAME'] = 's3fs_tests.php';
  $_SERVER['SCRIPT_FILENAME'] = 's3fs_tests.php';
  $_SERVER['QUERY_STRING'] = '';
  
  error_reporting(E_ALL | E_STRICT);
  require_once './includes/bootstrap.inc';
  drupal_bootstrap(DRUPAL_BOOTSTRAP_FULL);
  
  global $user;
  $user = user_load(array('name' => $username));
}

function parse_arguments($argc, $argv) {
  if ($argc < 2 || ($argc == 3 && $argv[2] != 'NOTEST') || $argc > 3) {
    fwrite(STDERR, "Usage: ". $argv[0] . " <filename> [NOTEST]\n\n");
    exit(1);
  }
  if ($argc == 2) {
    $argv[2] = FALSE;
  }
  return array($argv[1], $argv[2]);
}

main();
?>