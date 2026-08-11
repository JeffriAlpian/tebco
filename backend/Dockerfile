FROM node:20-alpine

# Set working directory di dalam container
WORKDIR /app

# Copy file package.json dan install dependencies
COPY package*.json ./
RUN npm install --omit=dev

# Copy seluruh kode sumber backend
COPY . .

# Expose port yang digunakan
EXPOSE 3000

# Perintah untuk menjalankan aplikasi
CMD ["node", "index.js"]
