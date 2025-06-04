### Build/test container ###
# Define builder stage
FROM wasd-gamers:base AS builder

# Share work directory
COPY . /usr/src/project
WORKDIR /usr/src/project/build

# Build and test
RUN cmake ..
RUN make
RUN ctest --output-on_failure


### Deploy container ###
# Define deploy stage
FROM ubuntu:noble AS deploy

# Copy server output binary to "."
COPY --from=builder /usr/src/project/build/bin/webserver .
COPY --from=builder /usr/src/project/my_config .

# Copy docs and template files
COPY --from=builder /usr/src/project/docs /docs
COPY --from=builder /usr/src/project/templates /templates

# Copy static files
COPY --from=builder /usr/src/project/static /static

# Expose port 80 for GCP
EXPOSE 80

# Use ENTRYPOINT to specify the binary name
ENTRYPOINT ["./webserver"]

# Use CMD to specify arguments to ENTRYPOINT
CMD ["my_config"]

# Create a volume to write logs to
VOLUME ["/logs"]

# Create a volume for persistent data storage
VOLUME ["/mnt/storage"]
