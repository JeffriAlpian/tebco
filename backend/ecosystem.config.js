module.exports = {
  apps: [{
    name: "tebco-backend",
    script: "./index.js",
    watch: false,
    env: {
      NODE_ENV: "production",
    }
  }]
}
