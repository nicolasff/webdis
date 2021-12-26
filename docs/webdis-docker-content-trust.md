# Webdis and Docker Content Trust

Docker images for Webdis are signed using [Docker Content Trust (DCT)](https://docs.docker.com/engine/security/trust/). This means that you can verify that a Docker image you pulled for Webdis is legitimate and was built by the author of Webdis, rather than by an unknown third-party.

Docker images for Webdis are published on Docker Hub and Amazon Elastic Container Registry (ECR):

- On Docker Hub, they are under `nicolas/webdis`: https://hub.docker.com/r/nicolas/webdis
- On ECR, they are also under `nicolas/webdis`: https://gallery.ecr.aws/nicolas/webdis

**Important:** Only images starting with [release 0.1.12](https://github.com/nicolasff/webdis/releases/tag/0.1.12) are signed. Images starting with [release 0.1.19](https://github.com/nicolasff/webdis/releases/tag/0.1.19) are published as multi-architecture manifests, so validating them involves different steps.

## Docker Hub vs AWS Elastic Container Registry

An important difference between the two services is that Docker Hub supports notarized (signed) images, but ECR **does not** (it is apparently [in the works](https://d2908q01vomqb2.cloudfront.net/fe2ef495a1152561572949784c16bf23abb28057/2020/08/21/C3-ECR-Security-Best-Practices_072020_v3-no-notes.pdf#page=7)).

ℹ️ If you use Docker Hub, just use `docker trust inspect` and compare the signing keys to those in this article, regardless of the version of Webdis. The keys are listed in [the next section of this document](#-key-ids).

Things are significantly more complex if you want to validate images pulled from ECR. The main idea is to compare the hash of an image pulled from ECR with the hash of a (signed) image pulled from Docker Hub. If they match and if the Docker Hub image can be validated with `docker trust inspect`, you can be certain that you've downloaded the exact same image from ECR as the one from Docker Hub.

🛑 If the images don't match, or if the signatures in `docker trust inspect` do not use the same keys as the ones documented here, something is wrong and you should **not** run the unknown image.

## Validation with `docker trust inspect` (Docker Hub only)

🐳 This process applies **only** to images downloaded from Docker Hub.

To validate an image, use `docker trust inspect` followed by the image name and version, and compare the keys fingerprints listed in the output with the ones documented here.

First, pull the image:
```
$ docker pull nicolas/webdis:0.1.19
0.1.19: Pulling from nicolas/webdis
Digest: sha256:5de58646bae3ee52e05a65672532120b094682b79823291031ccb41533c21667
Status: Image is up to date for nicolas/webdis:0.1.19
docker.io/nicolas/webdis:0.1.19
```
Then, inspect its content trust metadata:
```
$ docker trust inspect --pretty nicolas/webdis:0.1.19

Signatures for nicolas/webdis:0.1.19

SIGNED TAG   DIGEST                                                             SIGNERS
0.1.19       5de58646bae3ee52e05a65672532120b094682b79823291031ccb41533c21667   (Repo Admin)

List of signers and their keys for nicolas/webdis:0.1.19

SIGNER      KEYS
nicolasff   dd0768b9d35d

Administrative keys for nicolas/webdis:0.1.19

  Repository Key:       fed0b56b8a8fd4d156fb2f47c2e8bd3eb61948b72a787c18e2fa3ea3233bba1a
  Root Key:    40be21f47831d593892370a8e3fc5bfffb16887c707bd81a6aed2088dc8f4bef
```

## 🔑 Key IDs

- The `SIGNER` field tells you who signed the image; it should be `nicolasff`. The short key ID is `dd0768b9d35d`, the full ID being `dd0768b9d35d344bbd1681418d27052c4c896a59be214352448daa2b6925b95b`.
- The Repository Key is scoped to the Docker Hub repo, `nicolas/webdis`. This should match as well. Its key ID is `fed0b56b8a8fd4d156fb2f47c2e8bd3eb61948b72a787c18e2fa3ea3233bba1a`.
- Finally, the Root Key ID is `40be21f47831d593892370a8e3fc5bfffb16887c707bd81a6aed2088dc8f4bef`.

Make sure that **all** the key IDs mentioned in the output of `docker trust inspect` are listed here.

✅ If you downloaded the image from Docker Hub, you can stop here.

## Validation of images downloaded from AWS ECR

Since ECR does not support Content Trust, the only way to validate the integrity of images downloaded from ECR is to download the same image from Docker Hub, validate its signature, and compare the image digests between the one from Docker Hub and the one from ECR.

It is tedious, but this seems to be the only workaround until AWS implements this feature.

## AWS ECR only: validation of single-architecture images (versions 0.1.12 to 0.1.18)

Given an image version, download it from both Docker Hub and AWS ECR. Let's do this for `0.1.18`.

First, from Docker Hub:
```
$ docker pull nicolas/webdis:0.1.18
0.1.18: Pulling from nicolas/webdis
Digest: sha256:6def97f1299c4de2046b1ae77427a7fa41552c91d3ae02059f79dbcb0650fe9e
Status: Image is up to date for nicolas/webdis:0.1.18
docker.io/nicolas/webdis:0.1.18
```

Then, from AWS ECR:
```
$ docker pull public.ecr.aws/nicolas/webdis:0.1.18
0.1.18: Pulling from nicolas/webdis
Digest: sha256:6def97f1299c4de2046b1ae77427a7fa41552c91d3ae02059f79dbcb0650fe9e
Status: Downloaded newer image for public.ecr.aws/nicolas/webdis:0.1.18
public.ecr.aws/nicolas/webdis:0.1.18
```

We can already see that the two lines starting with `Digest:` show the same hash.

To compare the images themselves, we can use `docker image inspect` and compare the `Id` fields:

```
$ docker image inspect nicolas/webdis:0.1.18 | grep -w Id
        "Id": "sha256:ecadadde26d4b78216b1b19e903a116ebcd824ae7f27963c5e3518ab1a58d859",

$ docker image inspect public.ecr.aws/nicolas/webdis:0.1.18 | grep -w Id
        "Id": "sha256:ecadadde26d4b78216b1b19e903a116ebcd824ae7f27963c5e3518ab1a58d859",
```

Both of them also have the same `RepoTags` in the full `docker image inspect` output.

Now that we know that the image we pulled from ECR is the exact same as the one from Docker Hub, we can follow the trust validation steps for Docker Hub [documented above](#validation-with-docker-trust-inspect-docker-hub-only) and know that since we could validate the signature of a Docker Hub image that is **identical** to our ECR image, the ECR image is also legit.

✅ If you wanted to validate an ECR image between `0.1.12` and `0.1.18`, you can stop here.

## AWS ECR only: validation of multi-architecture images (versions 0.1.19 and above)

Multi-architecture images are built using a _manifest list_, which is a small file that references multiple manifests. In turn, a Docker image manifest contains information about a single image, such as its size, layers, digest, etc:

```
 docker.io/nicolas/webdis:0.1.19
              │
              ▼
 ┌─Docker Hub Manifest List─┐
 │ ┌─────────────────────┐  │
 │ │ type: manifest.v2   │  │
 │ │ digest: sha256:AAAA─┼──┼────► docker.io/nicolas/webdis@sha256:AAAA
 │ │ platform:           │  │                │
 │ │   arch: amd64       │  │                ▼
 │ │   os: linux         │  │       ┌─Docker Hub Manifest─┐
 │ └─────────────────────┘  │       │ type: image.v1      │       ┌────Docker Image─────┐
 │ ┌─────────────────────┐  │       │ digest: sha256:FFFF─┼──────►│ Id: sha256:FFFF     │
 │ │ type: manifest.v2   │  │       │ size: 2737          │       │ Architecture: amd64 │
 │ │ digest: sha256:BBBB │  │       │ layers: 5 [...]     │       │ Os: linux           │
 │ │ platform:           │  │       └─────────────────────┘       │ Size: 11096513      │
 │ │   arch: arm64       │  │                                     │ Config:             │
 │ │   os: linux         │  │                                     │   ExposedPorts:     │
 │ │   variant: v8       │  │                                     │    - 7379/TCP       │
 │ └─────────────────────┘  │                                     │ Layers: [...]       │
 └──────────────────────────┘                                     └─────────────────────┘
 ```

With these images, it's the _manifest hash_ that is signed. By validating a signature on the manifest hash, you can guarantee that the contents of the manifest have not been altered which means you can trust the images that it points to.

## Structure of a multi-architecture Webdis release

Multi-architecture releases of Webdis use two manifest lists: one for Docker Hub and one for AWS Elastic Container Registry (ECR).
Each manifest list is tagged with the release itself (e.g. `0.1.19`), and points to two _manifests_ each describing an image of a single architecture at this version.

The entry point for a `docker pull` is just the repo and the version; here the repo could be on Docker Hub or AWS ECR.

If we run `docker manifest inspect` on a multi-architecture manifest, we get this manifest list with two entries:

```
$ docker manifest inspect docker.io/nicolas/webdis:0.1.19
{
   "schemaVersion": 2,
   "mediaType": "application/vnd.docker.distribution.manifest.list.v2+json",
   "manifests": [
      {
         "mediaType": "application/vnd.docker.distribution.manifest.v2+json",
         "size": 1365,
         "digest": "sha256:2ced2d99146e1bcaf10541d17dbac573cffd02237c3b268875be1868138d3b54",
         "platform": {
            "architecture": "amd64",
            "os": "linux"
         }
      },
      {
         "mediaType": "application/vnd.docker.distribution.manifest.v2+json",
         "size": 1365,
         "digest": "sha256:d026c5675552947b6a755439dfd58360e44a8860436f4eddfe9b26d050801248",
         "platform": {
            "architecture": "arm64",
            "os": "linux",
            "variant": "v8"
         }
      }
   ]
}
```

Each manifest is identified by its hash, and the `platform` metadata shows us the difference between the two manifests in this manifest list:
- For x86-64, the manifest hash is `sha256:2ced2d99146e1bcaf10541d17dbac573cffd02237c3b268875be1868138d3b54`.
- For ARM64v8, the manifest hash is `sha256:d026c5675552947b6a755439dfd58360e44a8860436f4eddfe9b26d050801248`.

Note that we didn't run `docker image inspect` since we're not dealing with images yet, only a manifest list so far.

If we look at the manifest for `amd64` (same as x86-64), we see that it references a single Docker image:
```
$ docker manifest inspect docker.io/nicolas/webdis@sha256:2ced2d99146e1bcaf10541d17dbac573cffd02237c3b268875be1868138d3b54
{
    "mediaType": "application/vnd.docker.distribution.manifest.v2+json",
    "schemaVersion": 2,
    "config": {
        "mediaType": "application/vnd.docker.container.image.v1+json",
        "digest": "sha256:010021e00e0910d0b73d0bfc9cb58ce583e96f3ad69fc6ee4a7a41baa707d7f7",
        "size": 2728
    },
    "layers": [ "... layers omitted here for brevity ..." ]
}
```
It's important to note that this manifest does not have a particular tag or human-assigned label/name, so we refer to it by its hash, and here again we ran `docker manifest inspect` since this is still not an image.

But this time, the manifest finally points to an image (see `mediaType`), with SHA-256 digest `010021e0…`. If instead of running `docker manifest inspect` we run `docker inspect` (with the same hash as in the previous command), this time we get the same familiar as the one for an image:

```
$ docker inspect docker.io/nicolas/webdis@sha256:2ced2d99146e1bcaf10541d17dbac573cffd02237c3b268875be1868138d3b54
[
    {
        "Id": "sha256:010021e00e0910d0b73d0bfc9cb58ce583e96f3ad69fc6ee4a7a41baa707d7f7",
        "RepoTags": [
            "nicolas/webdis:0.1.19",
            "nicolas/webdis:0.1.19-amd64",
            "nicolas/webdis:latest",
            "public.ecr.aws/nicolas/webdis:0.1.19-amd64"
        ],
        "...",
        "Config": {
            "Labels": {
                "org.opencontainers.image.base.name": "docker.io/library/alpine:3.14.3",
                "org.opencontainers.image.created": "2021-12-23T22:46:35-0800",
                "org.opencontainers.image.description": "Webdis 0.1.19",
                "org.opencontainers.image.licenses": "BSD-2-Clause",
                "org.opencontainers.image.revision": "417e0ac48345d849cd37db0a473d763b47195c23",
                "org.opencontainers.image.source": "https://github.com/nicolasff/webdis/tree/0.1.19",
                "org.opencontainers.image.title": "Webdis 0.1.19",
                "org.opencontainers.image.url": "https://hub.docker.com/r/nicolas/webdis",
                "org.opencontainers.image.version": "0.1.19"
            }
        },
        "Architecture": "amd64",
        "Os": "linux",
        "Size": 12078773,
        "..."
    }
]
```

## Validating trust for multi-architecture releases

Now that the structure is hopefully clear, let's look at how we can validate the integrity of our images.

The main challenge here is with AWS ECR, so let's start with Docker Hub manifest lists.
With Docker Hub, we can still use `docker trust inspect repo/image:version` to validate its signatures.

Here's its output, cleaned up with `grep` and `sed` for brevity but without losing any information:
```
$ docker trust inspect docker.io/nicolas/webdis:0.1.19 | grep -Ew '[A-Z][A-Za-z]+' | sed -E -e 's/ {8}/  /g' | sed -E -e 's/"|(^ {2})|((\[|,)$)//g'
Name: docker.io/nicolas/webdis:0.1.19
SignedTags:
  SignedTag: 0.1.19
  Digest: 5de58646bae3ee52e05a65672532120b094682b79823291031ccb41533c21667
  Signers:
      Repo Admin
Signers:
  Name: nicolasff
  Keys:
    ID: dd0768b9d35d344bbd1681418d27052c4c896a59be214352448daa2b6925b95b
AdministrativeKeys:
  Name: Root
  Keys:
    ID: 40be21f47831d593892370a8e3fc5bfffb16887c707bd81a6aed2088dc8f4bef
  Name: Repository
  Keys:
    ID: fed0b56b8a8fd4d156fb2f47c2e8bd3eb61948b72a787c18e2fa3ea3233bba1a
```

To validate the keys, refer to the list above in the "Key IDs" section.

### Validating the signed object

The `SignedTag` at the beginning of the output mentions `0.1.19`, but also a digest. This is the digest of the Manifest List that the name `0.1.19` points to. You can compute this digest yourself to make sure it matches what `docker trust inspect` returned.

ℹ️ An important point here is that Docker computes digests **without** a terminating new line for the JSON being hashed, but in a terminal it always adds it for readability. To compute the digest, you need to remove this new line; you can do this with `| perl -pe 'chomp if eof'`. All together:

```
$ docker manifest inspect docker.io/nicolas/webdis:0.1.19 | perl -pe 'chomp if eof' | shasum -a 256
5de58646bae3ee52e05a65672532120b094682b79823291031ccb41533c21667  -
```
(change `shasum -a 256` to `sha256sum` on GNU/Linux)

✅ Note that this matches the hash we found in `SignedTag`.

With this, we **know** that the manifest list was not altered. From the manifest list, we can find the two manifests for the two architectures, and from those we can verify that the image digests referenced on Docker Hub are the same as the image digests referenced on AWS ECR.

The chain of trust goes like this:

- Docker Hub manifest list (signed)
  - Docker Hub manifest for x86-64 (referenced in list whose matching hash we checked)
    - Image for x86-64 (transitively validated)
- AWS ECR manifest list (not signed)
  - AWS ECR manifest for x86-64 (not signed)
    - Image for x86-64 (same hash as the one referenced in Docker Hub ⇒ therefore can be trusted)

The same applies for ARM64v8 images, of course.

### Putting it all together

Validate the signatures with Docker Hub:
```
$ docker trust inspect --pretty docker.io/nicolas/webdis:0.1.19
[ ... make sure the keys are all valid ... ]
```

Extract the manifest hashes from the Docker Hub manifest list:
```
$ docker manifest inspect nicolas/webdis:0.1.19 | grep -E 'digest|architecture'
         "digest": "sha256:2ced2d99146e1bcaf10541d17dbac573cffd02237c3b268875be1868138d3b54",
            "architecture": "amd64",
         "digest": "sha256:d026c5675552947b6a755439dfd58360e44a8860436f4eddfe9b26d050801248",
            "architecture": "arm64",
```

Examine the Docker Hub manifest for the architecture you're running:
```
$ docker manifest inspect nicolas/webdis@sha256:2ced2d99146e1bcaf10541d17dbac573cffd02237c3b268875be1868138d3b54 | grep -A3 config
    "config": {
        "mediaType": "application/vnd.docker.container.image.v1+json",
        "digest": "sha256:010021e00e0910d0b73d0bfc9cb58ce583e96f3ad69fc6ee4a7a41baa707d7f7",
        "size": 2728
```

Note the digest: `sha256:010021e00e0910d0b73d0bfc9cb58ce583e96f3ad69fc6ee4a7a41baa707d7f7`.

Repeat the last two steps for ECR, first extracting the manifest hashes from the ECR manifest list:

```
$ docker manifest inspect public.ecr.aws/nicolas/webdis:0.1.19 | grep -E 'digest|architecture'
         "digest": "sha256:ec6a77ec083a659d3293810542c07bc1eee74e148cb02448cca3bfb260d7c19c",
            "architecture": "amd64",
         "digest": "sha256:d78f48b96464cd31bb1c29b01bcdaceac28c2ccb2d52a294cdf4cf840f5b6433",
            "architecture": "arm64",
```

These are different, but do they point to the same images?

```
$ docker manifest inspect public.ecr.aws/nicolas/webdis@sha256:ec6a77ec083a659d3293810542c07bc1eee74e148cb02448cca3bfb260d7c19c | grep -A3 config
    "config": {
        "mediaType": "application/vnd.docker.container.image.v1+json",
        "size": 2728,
        "digest": "sha256:010021e00e0910d0b73d0bfc9cb58ce583e96f3ad69fc6ee4a7a41baa707d7f7"
```

✅ Yes, this is the same image as the trusted image fom Docker Hub.

## Relationship diagram between Manifest Lists, Manifests, and Images

In case this helps make sense of the way all these objects are connected and reference each other, here's a diagram. Note how the two Manifest Lists point to different Manifests, but that these Manifests point to the same Images.

The Manifest List "entry point" is underlined in bold.

```
                                 ┌────Docker Image─────┐
                                 │ Id: sha256:FFFF     │
                                 │ Architecture: amd64 │ (both manifests reference the same image)
                                 │ Os: linux           │
                                 │ Size: 11096513      │
                                 └─────────────────────┘
                                          ▲  ▲
                                          │  └───────────────────────────────┐
        ┌─────►┌─Docker Hub Manifest─┐    │     ┌─►┌─Docker Hub Manifest─┐   │
        │      │ type: image.v1      │    │     │  │ type: image.v1      │   │
        │      │ digest: sha256:FFFF─┼────┘     │  │ digest: sha256:FFFF─┼───┘
        │      │ layers: 5 [...]     │          │  │ layers: 5 [...]     │
        │      └─────────────────────┘          │  └─────────────────────┘
        │                                       │
        └─docker.io/nicolas/webdis@sha256:AAAA  └──public.ecr.aws/nicolas/webdis@sha256:CCCC
                                          ▲                                             ▲
   DOCKER HUB ENTRY POINT:                │       AWS ECR ENTRY POINT:                  │
┌──docker.io/nicolas/webdis:0.1.19        │    ┌──public.ecr.aws/nicolas/webdis:0.1.19  │
│  ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀        │    │  ▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀  │
│                                         │    │                                        │
└───────────►┌─Docker Hub Manifest List─┐ │    └──►┌──AWS ECR Manifest List───┐         │
             │  (v0.1.19, 2 manifests)  │ │        │  (v0.1.19, 2 manifests)  │         │
             │ ┌─────────────────────┐  │ │        │ ┌─────────────────────┐  │         │
             │ │ type: manifest.v2   │  │ │        │ │ type: manifest.v2   │  │         │
             │ │ digest: sha256:AAAA─┼──┼─┘        │ │ digest: sha256:CCCC─┼──┼─────────┘
             │ │ arch: amd64         │  │          │ │ arch: amd64         │  │
             │ │ os: linux           │  │          │ │ os: linux           │  │
             │ └─────────────────────┘  │          │ └─────────────────────┘  │
    (signed) │ ┌─────────────────────┐  │          │ ┌─────────────────────┐  │ (not signed)
             │ │ type: manifest.v2   │  │          │ │ type: manifest.v2   │  │
             │ │ digest: sha256:BBBB─┼──┼─┐        │ │ digest: sha256:DDDD─┼──┼─────────┐
             │ │ arch: arm64         │  │ │        │ │ arch: arm64         │  │         │
             │ │ os: linux           │  │ │        │ │ os: linux           │  │         │
             │ │ variant: v8         │  │ │        │ │ variant: v8         │  │         │
             │ └─────────────────────┘  │ │        │ └─────────────────────┘  │         │
             └──────────────────────────┘ │        └──────────────────────────┘         │
                                          ▼                                             ▼
       ┌──docker.io/nicolas/webdis@sha256:BBBB   ┌─public.ecr.aws/nicolas/webdis@sha256:DDDD
       │                                         │
       └──────►┌─Docker Hub Manifest─┐           └─►┌──AWS ECR Manifest───┐
               │ type: image.v1      │              │ type: image.v1      │
               │ digest: sha256:EEEE─┼─┐            │ digest: sha256:EEEE─┼─┐
               │ layers: 5 [...]     │ │            │ layers: 5 [...]     │ │
               └─────────────────────┘ │            └─────────────────────┘ │
                                       │ ┌──────────────────────────────────┘
                                       ▼ ▼
                             ┌────Docker Image─────┐
                             │ Id: sha256:EEEE     │
                             │ Architecture: arm64 │ (both manifests reference the same image)
                             │ Os: linux           │
                             │ Size: 12201637      │
                             └─────────────────────┘
```
