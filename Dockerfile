# SPDX-License-Identifier: MPL-2.0

FROM docker.io/zephyrprojectrtos/ci:v0.29.4

WORKDIR /workdir

COPY west.yml /workdir/oskey/west.yml
RUN west init -l /workdir/oskey
RUN west config manifest.group-filter -- +optional && west update --narrow -o=--depth=1
COPY patch /workdir/oskey/patch
RUN python3 /workdir/oskey/patch/apply.py /workdir
RUN west blobs fetch hal_espressif || west blobs fetch hal_espressif

ENV PATH="/root/.cargo/bin:${PATH}"
RUN rustup toolchain install 1.97.1 --profile minimal && rustup default 1.97.1
RUN rustup target install thumbv7em-none-eabi thumbv7em-none-eabihf

RUN cargo install espup --version 0.16.0 --locked
RUN espup install --toolchain-version 1.95.0.0 --stable-version 1.97.1
RUN echo '. /root/export-esp.sh' >> ~/.bashrc
ENV BASH_ENV=/root/export-esp.sh

RUN apt update && apt install curl && curl -fsSL https://deno.land/install.sh | sh

ENV PATH="/root/.deno/bin:$PATH"

WORKDIR /workdir/oskey
