FROM docker.io/zephyrprojectrtos/zephyr-build:main

WORKDIR /workdir

RUN west init && west update
RUN west config manifest.group-filter -- +optional && west update

RUN wget -q -O-  "https://sh.rustup.rs" | sh -s -- -y
ENV PATH="/home/user/.cargo/bin:${PATH}"

RUN rustup target install riscv32i-unknown-none-elf
RUN rustup target install riscv32imc-unknown-none-elf
RUN rustup target install riscv64imac-unknown-none-elf
RUN rustup target install thumbv6m-none-eabi
RUN rustup target install thumbv7m-none-eabi
RUN rustup target install thumbv7em-none-eabi
RUN rustup target install thumbv7emhf-none-eabi
RUN rustup target install thumbv8m.main-none-eabi
RUN rustup target install thumbv8m.main-none-eabihf
RUN rustup target install thumbv8m.base-none-eabi
RUN rustup target install x86_64-unknown-none
RUN rustup target install aarch64-unknown-none

RUN cargo install espup

WORKDIR /workdir/oskey

