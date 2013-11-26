S3 File System (s3fs) provides an additional file system to your drupal site,
alongside the public and private file systems, which stores files in Amazon's
Simple Storage Service (S3) (or any S3-compatible storage service). You can set
your site to use S3 File System as the default, or use it only for individual
fields. This functionality is designed for sites which are load-balanced across
multiple servers, as the mechanism used by Drupal's default file systems is not
viable under such a configuration.


== Dependencies ==
Libraries API 2.x - https://drupal.org/project/libraries
AWS SDK for PHP 2 (library) = http://aws.amazon.com/sdkforphp/


== Additional Requirements ==
PHP 5.3.3+ is required. The AWS SDK will not work on earlier versions.
Your PHP must be configured with "allow_url_fopen = On" in your php.ini file.
Otherwise, PHP will be unable to open files that are in your S3 bucket.


== Installation ==
Download and install the Libraries module (7.x-2.x) module from
http://drupal.org/project/libraries, then install AWS SDK for PHP 2.

You can install the SDK in three different ways:
1) (Recommended) Download and extract this zip to sites/all/libraries/awssdk2:
https://github.com/aws/aws-sdk-php/releases/download/2.4.10/aws.zip
2) Run this drush command from your site's root directory:
drush make sites/all/modules/s3fs/s3fs.make --no-core
3) Clone the following github repo into sites/all/libraries/awssdk2:
  https://github.com/coredumperror/aws-sdk-for-php2.git

For the SDK to work, you site's filesystem must have a file named
sites/all/libraries/awssdk2/aws-autoloader.php

If you keep your site in its own git repo and you used methods 2 or 3, be sure
to delete the sites/all/libraries/awssdk2/.git folder. Otherwise you may end
up with strange git submodule behavior.

== Configuring S3 File System ==
Configure your Amazon Web Services credentials by storing them in the $conf
array in your site's settings.php file, like so:
$conf['awssdk2_access_key'] = 'YOUR ACCESS KEY';
$conf['awssdk2_secret_key'] = 'YOUR SECRET KEY';

TODO: Consider making these settable from the UI.

Configure your setttings for S3 File System (including your S3 bucket name)
at /admin/config/media/s3fs/settings

With the settings saved, go to /admin/config/media/s3fs/actions to refresh the
file metadata cache. This will copy the filenames and attributes for every
existing file in your S3 bucket into Drupal's database. This can take a
significant amount of time for very large buckets (thousands of files).


== Using S3 File System ==
Visit admin/config/media/file-system and set the "Default download method" to
Amazon Simple Storage Service -and/or-
Add a field of type File, Image, etc and set the "Upload destination" to
Amazon Simple Storage Service in the Field Settings tab.


== Known Issues ==
Some curl libraries, such as the one bundled with MAMP, do not come
with authoritative certificate files. See the following page for details:
http://dev.soup.io/post/56438473/If-youre-using-MAMP-and-doing-something

== Acknowledgements ==
Special recognition goes to justafish, author of the AmazonS3 module:
http://drupal.org/project/amazons3
S3 File System is a from-the-ground-up re-write of her great module, with
powerful performance improvements and several new features.
