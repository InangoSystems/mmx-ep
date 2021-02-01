# Contributing to MMX

You can contribute to MMX in various ways:
* reporting bugs
* improving documentation or code

## Reporting bugs

To report a bug - open issue via GitHub WebUI with label "bug"

## Contributing code

Before development is started - create an issue:
* with label "bug" for bugs
* with label "enhancement" for task
* with label "documentation" for docs improvements

All development is managed using GitHub Merge Requests

[Commits](#commits) must be in logical, consistent units and have a good commit message.
Every commit must carry a Signed-off-by tag to assert your agreement with the [DCO](#developers-certificate-of-origin).
Merge requests are reviewed and checks are performed on them before they are merged.
Code should adhere to the [coding style](#coding-style).

### Commits

The code history is considered a very important aspect of the source code of the project.
Please pay attention on the commits comment and content.

Commits should be split up into atomic units that perform one specific change.

A commit message consists of a subject, a message body and a set of tags.

    short title of commit

    The current situation is this and that. However, it should be that or
    that.

    We can do one or the other. One has this advantage, the other has this
    advantage. We choose one.

    Do this thing and do that thing 

    Signed-off-by: The Author <author.mail@address.com>
    Co-Authored-by: The Other Author <email@address.com>
    Signed-off-by: The Other Author <email@address.com>

Write it in the imperative: "add support for X"
Avoid verbs that don't say anything: "fix", "improve", "update", ...

At the end of the commit message there is a block of tags.

The first tag must be a "Signed-off-by:" from the author of the commit.
This is a short way for you to say that you are entitled to contribute the patch under MMX's BSD+Patent license.
It is a legal statement that asserts your agreement with the [DCO](#developers-certificate-of-origin).
Therefore, it must have your *real name* (First Last) and a valid e-mail address.
Adding this tag can be done automatically by using `git commit -s`.
If you are editing files and committing through GitHub, you must write your real name in the “Full Name” field in your GitHub profile and the email used in the "Signed-off-by:" must be your "Commit email" address.
You must manually add the "Signed-off-by:" to the commit message that GitHub requests.
If you are editing files and committing on your local PC, set your name and email with:

```bash
git config --global user.name "my name"
git config --global user.email "my@email.address"
```

Then, adding the "Signed-off-by" line is as simple as using `git commit -s` instead of `git commit` (using an alias is recommended, e.g. `git config --global alias.ci='commit -s'`)

<a id="coding-style"></a>
### Coding style

TBD

Note: write a new code in the same style as exist one.

<a id="developers-certificate-of-origin"></a>
## Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

* (a) The contribution was created in whole or in part by me and I
  have the right to submit it under the open source license
  indicated in the file; or

* (b) The contribution is based upon previous work that, to the best
  of my knowledge, is covered under an appropriate open source
  license and I have the right under that license to submit that
  work with modifications, whether created in whole or in part
  by me, under the same open source license (unless I am
  permitted to submit under a different license), as indicated
  in the file; or

* (c) The contribution was provided directly to me by some other
  person who certified (a), (b) or (c) and I have not modified
  it.

* (d) I understand and agree that this project and the contribution
  are public and that a record of the contribution (including all
  personal information I submit with it, including my sign-off) is
  maintained indefinitely and may be redistributed consistent with
  this project or the open source license(s) involved.

