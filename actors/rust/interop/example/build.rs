// Links the precompiled C++ objects (the generated bridge + the C++ actor setup)
// and the C++ actor-framework static library into this Rust binary, so `cargo`
// (rustc) does the final link and handles the Rust runtime.
//
// Prerequisites (see README): build the C++ framework (`cd actors/cpp && make opt`)
// and the C++ objects (`bash cpp/build_cpp.sh`).
//
// Homebrew (macOS) paths are the defaults; on Linux, set BOOST_LIB_DIR / ZMQ_LIB_DIR
// / CRYPTOPP_LIB_DIR to match your install.
use std::path::PathBuf;

fn main() {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR")); // .../actors/rust/interop/example
    let cpp = manifest.join("cpp");

    for obj in ["hybrid_cpp.o", "CppActorBridge.o"] {
        let p = cpp.join(obj);
        assert!(p.exists(), "missing {} — run `bash cpp/build_cpp.sh` first", p.display());
        println!("cargo:rustc-link-arg={}", p.display());
        println!("cargo:rerun-if-changed={}", p.display());
    }

    // C++ actor-framework static library: actors/cpp/libactors.a
    let actors_cpp = manifest.join("../../../cpp");
    println!("cargo:rustc-link-search=native={}", actors_cpp.display());
    println!("cargo:rustc-link-lib=static=actors");

    let dir = |key: &str, default: &str| std::env::var(key).unwrap_or_else(|_| default.into());
    for (d, libs) in [
        (dir("BOOST_LIB_DIR", "/opt/homebrew/opt/boost/lib"), &["boost_system", "boost_thread", "boost_filesystem"][..]),
        (dir("ZMQ_LIB_DIR", "/opt/homebrew/opt/zeromq/lib"), &["zmq"][..]),
        (dir("CRYPTOPP_LIB_DIR", "/opt/homebrew/opt/cryptopp/lib"), &["cryptopp"][..]),
    ] {
        println!("cargo:rustc-link-search=native={d}");
        for l in libs {
            println!("cargo:rustc-link-lib=dylib={l}");
        }
    }
    println!("cargo:rustc-link-lib=dylib=c++"); // for the linked C++ objects
}
