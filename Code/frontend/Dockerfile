FROM node:22-alpine

# The repo is bind-mounted at run time and node_modules lives inside the mounted
# frontend/ dir (gitignored), so the image itself only needs Node + npm.
# See frontend/README.md for the build / dev commands.
WORKDIR /app

# Default action: install deps (cached in the mounted node_modules) and build
# the committed frontend bundle into ../data.
CMD ["sh", "-c", "npm install && npm run build"]
