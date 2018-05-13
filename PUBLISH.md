# How to publish the GNU MCU Eclipse OpenOCD binaries?

## Update the Change log

Generally, apart from packing, there should be no local changes compared 
to the original OpenOCD distribution.

Open the `CHANGELOG.txt` file from  
`gnu-mcu-eclipse/openocd-build.git` project git, and copy 
entries to the web git.

In the web git, add new entries to the 
[Change log](https://gnu-mcu-eclipse.github.io/openocd/change-log/) 
(`pages/openocd/change-log.md`), grouped by days.

Note: if you missed to update the `CHANGELOG.txt` before starting the build, 
edit the file and rerun the build, it should take only a few minutes to 
recreate the archives with the correct file.

## Edit the build script

Edit the `VERSION` file to refer to the actual release.

## Push the build script git

Push `gnu-mcu-eclipse/openocd-build.git` to GitHub.

Possibly push the helper project too.

## Build

Follow the instructions on the 
[build](https://github.com/gnu-mcu-eclipse/openocd-build/blob/master/README.md) 
page.

## Prepare a new blog post 

In the `gnu-mcu-eclipse.github.io-source.git` web git:

- add a new file to `_posts/openocd/releases`
- name the file like `2018-05-12-openocd-v0-10-0-8-20180512-released.md`
- name the post like: **GNU MCU Eclipse OpenOCD v0.10.0-8-20180512 released**.
- as `download_url` use the generic `https://github.com/gnu-mcu-eclipse/openocd/releases/` 
- update the `date:` field with the current date

If any, close [issues](https://github.com/gnu-mcu-eclipse/openocd/issues) 
on the way. Refer to them as:

- **[Issue:\[#1\]\(...\)]**.

## Update the SHA sums

Copy/paste the build report at the end of the post as:

```console
## Checksums
The SHA-256 hashes for the files are:

6d1baccebc2dd8667556b61e5b93aa83587d0d52430235bb954b364e5cc903e3 ?
gnu-mcu-eclipse-openocd-0.10.0-8-20180512-1921-centos32.tgz

44775b886139ae761b3ceca630651efeced43cbd7ad5683cdb70c5a4e6d83119 ?
gnu-mcu-eclipse-openocd-0.10.0-8-20180512-1921-centos64.tgz

f3fff5c6b72b680a7995d285f442185bff83e3a31842fa2a4f21c5a23dee24f3 ?
gnu-mcu-eclipse-openocd-0.10.0-8-20180512-1921-macos.tgz

7a2bc7b751127535b967a73215788d3a66633e2b33e4edd02bb9d70a1cde7ed9 ?
gnu-mcu-eclipse-openocd-0.10.0-8-20180512-1921-win32.zip

1be649d32a2e0895c84da6570d427dee2ed89f48ca6a3547d0d08e311c536ad4 ?
gnu-mcu-eclipse-openocd-0.10.0-8-20180512-1921-win64.zip
```

## Update the Web

- commit the `gnu-mcu-eclipse.github.io-source` project; use a message 
like **GNU MCU Eclipse OpenOCD v0.10.0-8 released**
- wait for the Travis build to complete; occasionally links to not work,
 and might need to restart the build.
- remember the post URL, since it must be updated in the release page

Note: initially the link to binaries points to the parent releases folder, 
otherwise Travis will complain and do not publish the site to 
`gnu-mcu-eclipse.github.io`.

## Create a new GitHub release

- go to the [GitHub Releases](https://github.com/gnu-mcu-eclipse/openocd/releases) page
- click **Draft a new release**
- name the tag like **v0.10.0-8-20180512** (mind the dash in the middle!)
- name the release like **GNU MCU Eclipse OpenOCD v0.10.0-8-20180512** 
(mind the dash and the space)
- as description
  - add a downloads badge like `[![Github Releases (by Release)](https://img.shields.io/github/downloads/gnu-mcu-eclipse/openocd/v0.10.0-8-20180512/total.svg)]()`; use empty URL for now
  - copy the first paragraph from the Web release page
- add a link to the Web page `[Continue reading »]()`; use an empty URL for now
- get URL from web and update the above links
- **attach binaries** and SHA (drag and drop from the archives folder will do it)
- click the **Publish Release** button

Note: at this moment the system should send a notification to all clients watching this project.

## Update the web link 

In the web git:

- `download_url: https://github.com/gnu-mcu-eclipse/openocd/releases/tag/v0.10.0-8-20180512`
- use something like `v0.10.0-8 update link` as message

## Create the xPack release

Follow the instructions on the 
[openocd-xpack](https://github.com/gnu-mcu-eclipse/openocd-xpack/blob/xpack/README.md#maintainer-info)
page.

## Update the release with xPack easy install

- copy the **Easy install** section from a previous release
- update release number

## Share on Facebook

- go to the new post and follow the Share link.
- DO NOT select **On your own Timeline**, but **On a Page you manage**
- select GNU MCU Eclipse
- posting as GNU MCU Eclipse
- click **Post to Facebook**
- check the post in the [Facebook page](https://www.facebook.com/gnu-mcu-eclipse)

## Links

- [Submitting patches to the OpenOCD Gerrit server](http://openocd.org/doc-release/doxygen/patchguide.html)
