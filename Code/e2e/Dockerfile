FROM mcr.microsoft.com/playwright:v1.61.0-noble

WORKDIR /tests

COPY package.json ./
RUN npm install

COPY playwright.config.js ./
COPY tests ./tests

ENV CI=1

CMD ["npm", "test"]
