# How to publish the GNU MCU Eclipse OpenOCD binaries?

## Build

Before starting the build, perform some checks.

### Check the CHANGELOG file

Open the `CHANGELOG.txt` file from  
`gnu-mcu-eclipse/openocd-build.git` project git, and and check if all 
new entries are in.

Generally, apart from packing, there should be no local changes compared 
to the original OpenOCD distribution.

Note: if you missed to update the `CHANGELOG.txt` before starting the build, 
edit the file and rerun the build, it should take only a few minutes to 
recreate the archives with the correct file.

### Check the version

The `VERSION` file should refer to the actual release.

### Push the build script git

In `gnu-mcu-eclipse/openocd-build.git`, if necessary, merge 
the `develop` branch into `master`.

Push it to GitHub.

Possibly push the helper project too.

### Run the build scripts

When everything is ready, follow the instructions on the 
[build](https://github.com/gnu-mcu-eclipse/openocd-build/blob/master/README.md) 
page.

## Test

Install the binaries on all supported platforms and check if they are 
functional.

## Create a new GitHub pre-release

- go to the [GitHub Releases](https://github.com/gnu-mcu-eclipse/openocd/releases) page
- click **Draft a new release**
- name the tag like **v0.10.0-8-20180512** (mind the dashes in the middle!)
- select the `master` branch
- name the release like **GNU MCU Eclipse OpenOCD v0.10.0-8 20180512** 
(mind the dash and the space)
- as description
  - add a downloads badge like `[![Github Releases (by Release)](https://img.shields.io/github/downloads/gnu-mcu-eclipse/openocd/v0.10.0-8-20180512/total.svg)]()`; use empty URL for now
  - draft a short paragraph explaining what are the main changes
- **attach binaries** and SHA (drag and drop from the archives folder will do it)
- enable the **pre-release** button
- click the **Publish Release** button

Note: at this moment the system should send a notification to all clients watching this project.

## Prepare a new blog post 

In the `gnu-mcu-eclipse.github.io-source.git` web git:

- add a new file to `_posts/openocd/releases`
- name the file like `2018-05-12-openocd-v0-10-0-8-20180512-released.md`
- name the post like: **GNU MCU Eclipse OpenOCD v0.10.0-8 20180512 released**.
- as `download_url` use the tagged URL `https://github.com/gnu-mcu-eclipse/openocd/releases/tag/v0.10.0-8-20180512/` 
- update the `date:` field with the current date

If any, close [issues](https://github.com/gnu-mcu-eclipse/openocd/issues) 
on the way. Refer to them as:

- **[Issue:\[#1\]\(...\)]**.

## Update the SHA sums

Copy/paste the build report at the end of the post as:

```console
## Checksums
The SHA-256 hashes for the files are:

6d1baccebc2dd8667556b61e5b93aa83587d0d52430235bb954b364e5cc903e3 
gnu-mcu-eclipse-openocd-0.10.0-8-20180512-1921-centos32.tgz

44775b886139ae761b3ceca630651efeced43cbd7ad5683cdb70c5a4e6d83119 
gnu-mcu-eclipse-openocd-0.10.0-8-20180512-1921-centos64.tgz

f3fff5c6b72b680a7995d285f442185bff83e3a31842fa2a4f21c5a23dee24f3 
gnu-mcu-eclipse-openocd-0.10.0-8-20180512-1921-macos.tgz

7a2bc7b751127535b967a73215788d3a66633e2b33e4edd02bb9d70a1cde7ed9 
gnu-mcu-eclipse-openocd-0.10.0-8-20180512-1921-win32.zip

1be649d32a2e0895c84da6570d427dee2ed89f48ca6a3547d0d08e311c536ad4 
gnu-mcu-eclipse-openocd-0.10.0-8-20180512-1921-win64.zip
```

If you missed this, `cat` the content of the `.sha` files:

```console
$ cd deploy
$ cat *.sha
```

## Update the Web

- commit the `gnu-mcu-eclipse.github.io-source` project; use a message 
like **GNU MCU Eclipse OpenOCD v0.10.0-8 20180512 released**
- wait for the Travis build to complete; occasionally links to not work,
 and might need to restart the build.
- remember the post URL, since it must be updated in the release page

## Create the xPack release

Follow the instructions on the 
[openocd-xpack](https://github.com/gnu-mcu-eclipse/openocd-xpack/blob/xpack/README.md#maintainer-info)
page.

## Create a final GitHub release

- go to the [GitHub Releases](https://github.com/gnu-mcu-eclipse/openocd/releases) page
- update the link behind the badge with the blog URL
- add a link to the Web page `[Continue reading »]()`; use an same blog URL
- copy/paste the **Easy install** section
- update the current release version
- copy/paste the **Download analytics** section
- update the current release version
- **disable** the **pre-release** button
- click the **Update Release** button

## Share on Facebook

- go to the new post and follow the Share link.
- DO NOT select **On your own Timeline**, but **On a Page you manage**
- select GNU MCU Eclipse
- posting as GNU MCU Eclipse
- click **Post to Facebook**
- check the post in the [Facebook page](https://www.facebook.com/gnu-mcu-eclipse)

## Share on Twitter

* go to the new post and follow the Tweet link
* copy the content to the clipboard
* DO NOT click the Tweet button here, it'll not use the right account
* in a separate browser windows, open [TweetDeck](https://tweetdeck.twitter.com/)
* using the `@gnu_mcu_eclipse` account, paste the content
* click the Tweet button

## Links

- [Submitting patches to the OpenOCD Gerrit server](http://openocd.org/doc-release/doxygen/patchguide.html)
