{
  "targets": [
    {
      "target_name": "dotnode",
      "cflags!": ["-fno-exceptions"],
      "cflags_cc!": ["-fno-exceptions"],
      "sources": ["src/native/dotnode.cpp"],
      "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"],
      "conditions": [
        ["OS=='linux'", { "defines": ["LINUX"] }],
        ["OS=='mac'", { "defines": ["LINUX", "OSX"] }],
        ["OS=='win'", { "defines": ["WINDOWS"] }]
      ],
      "include_dirs": ["<!@(node -p \"require('node-addon-api').include\")"]
    }
  ]
}
