FROM docker.io/zephyrprojectrtos/ci:v0.28.4

WORKDIR /workdir

RUN west init --mr v4.2.0
RUN west config manifest.group-filter -- +optional && west update

ENV PATH="/root/.cargo/bin:${PATH}"
RUN rustup target install riscv32imc-unknown-none-elf
RUN rustup target install thumbv7em-none-eabihf

RUN cargo install espup --locked
RUN espup install
RUN echo '. /root/export-esp.sh' >> ~/.bashrc

RUN wget https://raw.githubusercontent.com/butterfly-community/oskey-firmware/refs/heads/master/patch/rust.patch -P /workdir/modules/lang/rust
RUN cd /workdir/modules/lang/rust && git apply rust.patch

RUN apt update && apt install curl && curl -fsSL https://deno.land/install.sh | sh

ENV PATH="/root/.deno/bin:$PATH"

WORKDIR /workdir/oskey