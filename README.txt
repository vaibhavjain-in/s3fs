S3 File System (s3fs) provides an additional file system to your drupal site,
alongside the public and private file systems, which stores files in Amazon's
Simple Storage Service (S3) (or any S3-compatible storage service). You can set
your site to use S3 File System as the default, or use it only for individual
fields. This functionality is designed for sites which are load-balanced across
multiple servers, as the mechanism used by Drupal's default file systems is not
viable under such a configuration.

=========================================
== Dependencies and Other Requirements ==
=========================================
- Libraries API 2.x - https://drupal.org/project/libraries
- AWS SDK for PHP (library) = http://aws.amazon.com/sdk-for-php
- PHP 5.3.3+ is required. The AWS SDK will not work on earlier versions.
- Your PHP must be configured with "allow_url_fopen = On" in your php.ini file.
  Otherwise, PHP will be unable to open files that are in your S3 bucket.

==================
== Installation ==
==================
1) Install Libraries version 2.x from http://drupal.org/project/libraries.

2) Install the AWS SDK for PHP by going to http://aws.amazon.com/sdk-for-php
and clicking the orange "AWS SDK for PHP" button. On the page you're sent to,
click the green "aws.zip" button. Extract that zip file into your Drupal site's
sites/all/libraries/awssdk2 folder such that the path to aws-autoloader.php is
sites/all/libraries/awssdk2/aws-autoloader.php

In the unlikely circumstance that the version of the SDK you downloaded causes
errors with S3 File System, you can download this version instead, which is
known to work:
https://github.com/aws/aws-sdk-php/releases/download/2.6.3/aws.zip

IN CASE OF TROUBLE DETECTING THE AWS SDK LIBRARY:
Ensure that the awssdk2 folder itself, and all the files within it, can be read
by your webserver. Usually this means that the user "apache" (or "_www" on OSX)
must have read permissions for the files, and read+execute permissions for all
the folders in the path leading to the awssdk2 files.

====================
== Initial Setup ==
====================
With the code installation complete, you must now configure s3fs to use your
Amazon Web Services credentials. To do so, store them in the $conf array in
your site's settings.php file (sites/default/settings.php), like so:
$conf['awssdk2_access_key'] = 'YOUR ACCESS KEY';
$conf['awssdk2_secret_key'] = 'YOUR SECRET KEY';

Configure your setttings for S3 File System (including your S3 bucket name) at
/admin/config/media/s3fs/settings. You can input your AWS credentials on this
page as well, but using the $conf array is reccomended.

==================== ESSENTAL STEP! DO NOT SKIP THIS! =========================
With the settings saved, go to /admin/config/media/s3fs/actions to refresh the
file metadata cache. This will copy the filenames and attributes for every
existing file in your S3 bucket into Drupal's database. This can take a
significant amount of time for very large buckets (thousands of files). If this
operation times out, you can also perform it using "drush s3fs-refresh-cache".

Please keep in mind that any time the contents of your S3 bucket change without
Drupal knowing about it (like if you copy some files into it manually using
another tool), you'll need to refresh the metadata cache again. S3FS assumes
that its cache is a canonical listing of every file in the bucket. Thus, Drupal
will not be able to access any files you copied into your bucket manually until
S3FS's cache learns of them. This is true of folders as well; s3fs will not be
able to copy files into folders that it doesn't know about.

=================================================================
== Tell Your Site to Use s3fs Instead of the Public Filesystem ==
=================================================================
Visit admin/config/media/file-system and set the "Default download method" to
"Amazon Simple Storage Service"
-and/or-
Add a field of type File, Image, etc and set the "Upload destination" to
"Amazon Simple Storage Service" in the "Field Settings" tab.

This will configure your site to store *uploaded* files in S3. Files which your
site creates automatically (such as aggregated CSS) will still be stored in the
public filesystem, because Drupal is hard-coded to use public:// for such
files. A future version of S3 File System will add support for storing these
files in S3, as well, but there is currently no ETA for this feature.

==========================
== Aggregated CSS in S3 ==
==========================
Because of the way browsers interpret relative URLs used in CSS files, if you
want your site's aggregated/public CSS to be placed in S3, you'll need to set
up your webserver as a proxy for those files. S3 File System will present all
public:// css files with the url prefix /s3fs-css/, so you need to set up your
webserver to proxy all URLs with that prefix into your S3 bucket.

For Apache, add this code to the right location* in your server's config:

ProxyRequests Off
SSLProxyEngine on
<Proxy *>
    Order deny,allow
    Allow from all
</Proxy>
ProxyPass /s3fs-css/ https://YOUR-BUCKET.s3.amazonaws.com/s3fs-public/
ProxyPassReverse /s3fs-css/ https://YOUR-BUCKET.s3.amazonaws.com/s3fs-public/

* The "right location" is implementation-dependent. Normally, placing these
lines at the bottom of your httpd.conf file should be sufficient. However, if
your site is configured to use SSL, you'll need to put these lines in the
VirtuaHost settings for both your normal and SSL sites.

The /s3fs-public/ subfolder is where s3fs stores the files from the public://
filesystem, to avoid name conflicts with files from the s3:// filesystem.

I'm unfamiliar with non-Apache webservers, but if you've set up a Drupal site
using a different server, I assume you can translate these directives to your
server's config format.

If you're using the "Use a Custom Host" option to store your files in a
non-Amazon file service, you'll need to change the proxy target to the
appropriate URL for your service.

If you're using the "S3FS Root Folder" option, you'll need to insert that
folder before the /s3fs-public/ part of the target URLs.

===========================================
== Upgrading from S3 File System 7.x-1.x ==
===========================================
s3fs 7.x-2.x is not 100% backwards-compatible with 7.x-1.x. Most things will
work the same, but if you were using certain options in 1.x, you'll need to
perform some manual intervention to handle the upgrade to 2.x.

The Partial Refresh Prefix setting has been replaced with the Root Folder
setting. Root Folder fulfills the same purpose, but the implementation is
sufficiently different that you'll need to re-configure your site, and
possibly rearrange the files in your S3 bucket to make it work.

With Root Folder, *everything* s3fs does is contained to the specified folder
in your bucket. s3fs acts like the root folder is the bucket root, which means
that the URIs for your files will not reflect the root folder's existence.
Thus, you won't need to configure anything else, like the "file directory"
setting of file and image fields, to make it work.

This is different from how Partial Refresh Prefix worked, because that prefix
*was* reflected in the uris, and you had to configure your file and image
fields appropriately.

So, when upgrading to 7.x-2.x, you'll need to set the Root Folder option to the
same value that you had for Partial Refresh Prefix, and then remove that folder
from your fields' "File directory" settings. Then, move every file that s3fs
previously put into your bucket into the Root Folder. And if there are other
files in your bucket that you want s3fs to know about, move them into there,
too. Then do a metadata refresh.

==================
== Known Issues ==
==================
Some curl libraries, such as the one bundled with MAMP, do not come
with authoritative certificate files. See the following page for details:
http://dev.soup.io/post/56438473/If-youre-using-MAMP-and-doing-something

Because of a bizzare limitation regarding MySQL's maximum index length for
InnoDB tables, the maximum uri length that S3FS supports is 250 characters.
That includes the full path to the file in your bucket, as the full folder
path is part of the uri.

eAccelerator, a deprecated opcode cache plugin for PHP, is incompatible with
AWS SDK for PHP. eAccelerator will corrupt the configuration settings for
the SDK's s3 client object, causing a variety of different exceptions to be
thrown. If your server uses eAccelerator, it is highly recommended that you
replace it with a different opcode cache plugin, as development on
eAccelerator ended over 2 years ago.

======================
== Acknowledgements ==
======================
Special recognition goes to justafish, author of the AmazonS3 module:
http://drupal.org/project/amazons3
S3 File System started as a fork of her great module, but has evolved
dramatically since then, becoming a very different beast. The main benefit of
using S3 File System over AmazonS3 is performance, especially for image-
related operations.
