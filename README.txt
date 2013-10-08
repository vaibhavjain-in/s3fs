S3 File System (s3fs) provides an additional file system to your drupal site,
alongside the public and private file systems, which stores files in Amazon's
Simple Storage Service (S3) (or any S3-compatible storage service). You can set
your site to use S3 File System as the default, or use it only for individual
fields. This functionality is designed for sites which are load-balanced across
multiple servers, as the mechanism used by Drupal's default file systems is not
viable under such a configuration.


Dependencies
Libraries API 2.x - https://drupal.org/project/libraries 
AWS SDK for PHP (module) - http://drupal.org/project/awssdk 
AWS SDK for PHP (library) = http://aws.amazon.com/sdkforphp/

Additional Requirements
Your PHP must be configured with "allow_url_fopen = On"
in your php.ini file. Otherwise, PHP will be unable to open files that are in
your S3 bucket.


Installation
Download and install the Libraries (7.x-2.x branch) and AWS SDK
modules http://drupal.org/project/libraries http://drupal.org/project/awssdk
(For installation of awssdk, you will need to download the Amazon SDK for PHP
and place it in sites/all/libraries/awdsdk ) http://aws.amazon.com/sdkforphp/

- Configure AWS SDK (using either the AWS SDK for PHP UI module, or storing the
- settings in your settings.php file's $conf array).

- Configure your bucket setttings at /admin/config/media/s3fs

- Refresh your file metadata cache using the button at the bottom of
- /admin/config/media/s3fs

You can then: - Visit admin/config/media/file-system and set the Default
download method to Amazon Simple Storage Service and/or - Add a field of type
File or Image etc and set the Upload destination to Amazon Simple Storage
Service in the Field Settings tab.


Known Issues
Some curl libraries, such as the one bundled with MAMP, do not come
with authoritative certificate files. http://dev.soup.io/post/56438473/If-youre-
using-MAMP-and-doing-something


Special recognition goes to justafish, author of the <a
href="https://drupal.org/project/amazons3">AmazonS3</a> module. S3 File System
is heavily inspired by her great module, but has been re-written from the ground
up to provide powerful performance improvements and other new features and
fixes.

