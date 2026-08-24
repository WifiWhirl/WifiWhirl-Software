import { render } from 'preact';
import { App } from './app';

// Render first so a failing transport can never blank the UI.
render(<App />, document.getElementById('app')!);
