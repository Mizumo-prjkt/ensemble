use std::fmt;
use std::time::Instant;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ScriptTrap {
    pub message: String,
    pub line: usize,
}

impl ScriptTrap {
    pub fn new(msg: impl Into<String>, line: usize) -> Self {
        let msg = msg.into();
        let message = if line > 0 {
            format!("[Line {}] {}", line, msg)
        } else {
            msg
        };
        Self { message, line }
    }
}

impl fmt::Display for ScriptTrap {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.message)
    }
}

impl std::error::Error for ScriptTrap {}

#[derive(Debug, Clone, Copy)]
pub struct ResourceLimits {
    /// 8 MB hard RAM cap per plugin
    pub max_memory_bytes: usize,
    /// Max AST steps per invocation
    pub max_steps: usize,
    /// 250ms execution timeout per call
    pub max_time_ms: u64,
    /// Call stack depth limit
    pub max_recursion_depth: usize,
    /// 5 MB persistent storage cap per plugin
    pub max_disk_storage_bytes: usize,
}

impl Default for ResourceLimits {
    fn default() -> Self {
        Self {
            max_memory_bytes: 8 * 1024 * 1024,
            max_steps: 150_000,
            max_time_ms: 250,
            max_recursion_depth: 64,
            max_disk_storage_bytes: 5 * 1024 * 1024,
        }
    }
}

pub struct ResourceGuard {
    limits: ResourceLimits,
    start_time: Instant,
    steps: usize,
    recursion_depth: usize,
    allocated_bytes: usize,
    peak_bytes: usize,
}

impl ResourceGuard {
    pub fn new(limits: ResourceLimits) -> Self {
        Self {
            limits,
            start_time: Instant::now(),
            steps: 0,
            recursion_depth: 0,
            allocated_bytes: 0,
            peak_bytes: 0,
        }
    }

    pub fn reset(&mut self) {
        self.steps = 0;
        self.recursion_depth = 0;
        self.start_time = Instant::now();
    }

    pub fn step(&mut self, line: usize) -> Result<(), ScriptTrap> {
        self.steps += 1;
        if self.steps > self.limits.max_steps {
            return Err(ScriptTrap::new(
                "Execution aborted: instruction step limit exceeded (infinite loop protection)",
                line,
            ));
        }
        // Check wall-clock time every 256 steps
        if (self.steps & 0xFF) == 0 {
            if self.start_time.elapsed().as_millis() as u64 > self.limits.max_time_ms {
                return Err(ScriptTrap::new(
                    "Execution aborted: time limit exceeded (250ms max)",
                    line,
                ));
            }
        }
        Ok(())
    }

    pub fn enter_frame(&mut self, line: usize) -> Result<(), ScriptTrap> {
        self.recursion_depth += 1;
        if self.recursion_depth > self.limits.max_recursion_depth {
            return Err(ScriptTrap::new(
                "Execution aborted: maximum recursion depth (64) exceeded",
                line,
            ));
        }
        Ok(())
    }

    pub fn leave_frame(&mut self) {
        if self.recursion_depth > 0 {
            self.recursion_depth -= 1;
        }
    }

    pub fn alloc_bytes(&mut self, bytes: usize, line: usize) -> Result<(), ScriptTrap> {
        if self.allocated_bytes.saturating_add(bytes) > self.limits.max_memory_bytes {
            return Err(ScriptTrap::new(
                format!(
                    "Memory Allocation Failure: script exceeded the {} KB memory cap!",
                    self.limits.max_memory_bytes / 1024
                ),
                line,
            ));
        }
        self.allocated_bytes += bytes;
        if self.allocated_bytes > self.peak_bytes {
            self.peak_bytes = self.allocated_bytes;
        }
        Ok(())
    }

    pub fn free_bytes(&mut self, bytes: usize) {
        self.allocated_bytes = self.allocated_bytes.saturating_sub(bytes);
    }

    pub fn current_memory_bytes(&self) -> usize {
        self.allocated_bytes
    }

    pub fn peak_memory_bytes(&self) -> usize {
        self.peak_bytes
    }

    pub fn current_steps(&self) -> usize {
        self.steps
    }

    pub fn elapsed_ms(&self) -> u64 {
        self.start_time.elapsed().as_millis() as u64
    }

    pub fn limits(&self) -> &ResourceLimits {
        &self.limits
    }

    pub fn set_limits(&mut self, limits: ResourceLimits) {
        self.limits = limits;
    }
}

impl Default for ResourceGuard {
    fn default() -> Self {
        Self::new(ResourceLimits::default())
    }
}
