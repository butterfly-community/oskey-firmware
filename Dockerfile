FROM docker.io/zephyrprojectrtos/ci:v0.29.2

WORKDIR /workdir

RUN west init --mr v4.4.1
RUN west config manifest.group-filter -- +optional && west update

ENV PATH="/root/.cargo/bin:${PATH}"
RUN rustup target install thumbv7em-none-eabihf

RUN cargo install espup --locked
RUN espup install
RUN echo '. /root/export-esp.sh' >> ~/.bashrc

RUN wget https://raw.githubusercontent.com/butterfly-community/oskey-firmware/refs/heads/master/patch/rust.patch -P /workdir/modules/lang/rust
RUN cd /workdir/modules/lang/rust && git apply rust.patch

RUN wget https://raw.githubusercontent.com/butterfly-community/oskey-firmware/refs/heads/master/patch/espressif.patch -P /workdir/modules/hal/espressif
RUN cd /workdir/modules/hal/espressif && git apply espressif.patch

RUN apt update && apt install curl && curl -fsSL https://deno.land/install.sh | sh

ENV PATH="/root/.deno/bin:$PATH"

WORKDIR /workdir/oskey
