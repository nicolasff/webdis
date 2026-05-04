# Webdis and image signing with cosign

Starting with release `0.1.24`, Docker images for Webdis are signed using [cosign](https://docs.sigstore.dev/cosign/), part of the [Sigstore](https://www.sigstore.dev/) project. This lets you verify that an image you pulled was published by the author of Webdis and has not been altered after publication.

> Releases `0.1.12` through `0.1.23` were signed using Docker Content Trust (Notary v1) instead. See the [legacy doc](webdis-docker-content-trust.md) for the older procedure; the steps on this page apply only to `0.1.24` and later.

## Public key

The signing key is an ECDSA P-256 key, published as [`webdis.pub`](../webdis.pub) at the root of this repository. To make sure you have an authentic copy, compute its SHA-256 fingerprint over the DER encoding and compare:

```sh
$ openssl pkey -in webdis.pub -pubin -outform DER | openssl dgst -sha256
SHA2-256(stdin)= c921084265695461eef46c6a5953b3dfc501c0f6f82f54ff05bdddb65670bc59
```

🛑 If your fingerprint does **not** match `c921084265695461eef46c6a5953b3dfc501c0f6f82f54ff05bdddb65670bc59`, stop. Either your key file or this documentation has been tampered with — investigate before continuing.

## Verifying an image

Pull the image and run `cosign verify`:

```sh
$ docker pull nicolas/webdis:0.1.24
$ cosign verify --key webdis.pub nicolas/webdis:0.1.24
```

A successful verification prints something like:

```
Verification for index.docker.io/nicolas/webdis:0.1.24 --
The following checks were performed on each of these signatures:
  - The cosign claims were validated
  - Existence of the claims in the transparency log was verified offline
  - The signatures were verified against the specified public key

[ <signature payload, JSON> ]
```

and exits with status 0. The three "checks" lines are what give you assurance:

- **cosign claims** — the signature payload references the digest of the manifest you just verified.
- **transparency log** — the signature was logged to [Rekor](https://docs.sigstore.dev/logging/overview/) at signing time, providing a public, append-only audit trail. You can independently search Rekor for this image's signatures at <https://search.sigstore.dev/> using the image digest.
- **public key** — the signature was produced by the holder of the private key matching `webdis.pub`.

Any other outcome — non-zero exit status, missing checks, key mismatch — means verification failed. Do not run the image.

## Recursive signing and multi-architecture images

Webdis is distributed as a multi-architecture OCI index (currently `linux/amd64` and `linux/arm64`). The index points to one manifest per architecture, and each manifest points to the actual image layers.

The release script signs with `--recursive`, which produces a signature for **every** manifest under the index — both the index itself and each per-architecture manifest. Verifying the tag (which resolves to the index) is enough for most users; the per-architecture manifests are also independently verifiable.

To verify a specific architecture, resolve its digest from the index and pass that to `cosign verify`:

```sh
$ docker buildx imagetools inspect nicolas/webdis:0.1.24 --raw \
    | jq -r '.manifests[] | "\(.platform.os)/\(.platform.architecture) \(.digest)"'
linux/amd64 sha256:aaaa...
linux/arm64 sha256:bbbb...

$ cosign verify --key webdis.pub nicolas/webdis@sha256:aaaa...
```

## Hub vs ECR

Both registries carry the same image digests and the same signatures. The release pipeline:

1. Builds the multi-arch image and pushes it to Docker Hub (a single `docker buildx --push` invocation).
2. Signs the resulting digest with cosign, recursively.
3. Mirrors the image to ECR with `oras copy -r`, which preserves the digest **and** copies the signature artifacts byte-for-byte.

Because the signatures are copied rather than regenerated, the same `cosign verify --key webdis.pub` command works against either registry:

```sh
$ cosign verify --key webdis.pub nicolas/webdis:0.1.24                  # Docker Hub
$ cosign verify --key webdis.pub public.ecr.aws/nicolas/webdis:0.1.24   # ECR
```

This is a meaningful change from earlier releases. Under Docker Content Trust, ECR images could not be signed at all, and trust had to be inferred indirectly by comparing image digests against the signed Docker Hub copy. With cosign, ECR signatures are first-class.

## Chain of trust

```
                              ┌─ webdis.pub ─┐
                              │ ECDSA P-256  │
                              └──────┬───────┘
                                     │ verifies
                                     ▼
                        ┌── cosign signature ──┐
                        │ over manifest digest │
                        └──────┬───────────────┘
                               │ binds
                               ▼
                nicolas/webdis@sha256:<index>                            (multi-arch index, signed)
                               │
                     ┌─────────┴─────────┐
                     ▼                   ▼
          amd64 manifest@sha256   arm64 manifest@sha256        (each independently signed via --recursive)
                     │                   │
                     ▼                   ▼
          amd64 image layers      arm64 image layers
```
